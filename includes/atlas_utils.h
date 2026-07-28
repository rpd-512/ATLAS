#ifndef ATLAS_UTILS_H
#define ATLAS_UTILS_H

#include <string>
#include <unordered_map>
#include <optional>
#include <stdexcept>

#include "io_utils.h"


using wire_id = uint32_t;
using node_id = uint32_t;

#include <unordered_map>
#include <string>
#include <vector>
#include <stdexcept>

struct GateData {
    std::string type;       // logical type, e.g. "AND2", "AOI21"
    float area;              // TODO: fill from liberty/.lib or OpenROAD JSON
    std::string name;        // actual sky130 cell name (drive strength 1 used here)
    int num_inputs;
    int num_outputs;

    std::vector<bool> evaluate(const std::vector<bool>& inputs) const;
};

std::unordered_map<std::string, GateData> gate_lib;

static void add_gate(std::unordered_map<std::string, GateData>& lib,
                      const std::string& logical_type,
                      const std::string& sky130_name,
                      int num_in, int num_out) {
    lib[logical_type] = GateData{logical_type, 0.0f, sky130_name, num_in, num_out};
}

void initialize_gate_library(std::unordered_map<std::string, GateData>& gate_lib) {
    // --- basic buffering / inversion ---
    add_gate(gate_lib, "INV",  "sky130_fd_sc_hd__inv_1",  1, 1);
    add_gate(gate_lib, "BUF",  "sky130_fd_sc_hd__buf_1",  1, 1);

    // --- AND family ---
    add_gate(gate_lib, "AND2", "sky130_fd_sc_hd__and2_1", 2, 1);
    add_gate(gate_lib, "AND3", "sky130_fd_sc_hd__and3_1", 3, 1);
    add_gate(gate_lib, "AND4", "sky130_fd_sc_hd__and4_1", 4, 1);

    // --- OR family ---
    add_gate(gate_lib, "OR2",  "sky130_fd_sc_hd__or2_1",  2, 1);
    add_gate(gate_lib, "OR3",  "sky130_fd_sc_hd__or3_1",  3, 1);
    add_gate(gate_lib, "OR4",  "sky130_fd_sc_hd__or4_1",  4, 1);

    // --- NAND family ---
    add_gate(gate_lib, "NAND2","sky130_fd_sc_hd__nand2_1",2, 1);
    add_gate(gate_lib, "NAND3","sky130_fd_sc_hd__nand3_1",3, 1);
    add_gate(gate_lib, "NAND4","sky130_fd_sc_hd__nand4_1",4, 1);

    // --- NOR family ---
    add_gate(gate_lib, "NOR2", "sky130_fd_sc_hd__nor2_1", 2, 1);
    add_gate(gate_lib, "NOR3", "sky130_fd_sc_hd__nor3_1", 3, 1);
    add_gate(gate_lib, "NOR4", "sky130_fd_sc_hd__nor4_1", 4, 1);

    // --- XOR / XNOR ---
    add_gate(gate_lib, "XOR2", "sky130_fd_sc_hd__xor2_1", 2, 1);
    add_gate(gate_lib, "XOR3", "sky130_fd_sc_hd__xor3_1", 3, 1);
    add_gate(gate_lib, "XNOR2","sky130_fd_sc_hd__xnor2_1",2, 1);
    add_gate(gate_lib, "XNOR3","sky130_fd_sc_hd__xnor3_1",3, 1);

    // --- MUX ---
    add_gate(gate_lib, "MUX2", "sky130_fd_sc_hd__mux2_1", 3, 1); // A, B, S
    add_gate(gate_lib, "MUX4", "sky130_fd_sc_hd__mux4_1", 6, 1); // A0-3, S0, S1

    // --- AOI family (AND-OR-INVERT) ---
    add_gate(gate_lib, "AOI21","sky130_fd_sc_hd__a21oi_1", 3, 1); // A1,A2,B1
    add_gate(gate_lib, "AOI22","sky130_fd_sc_hd__a22oi_1", 4, 1); // A1,A2,B1,B2
    add_gate(gate_lib, "AOI31","sky130_fd_sc_hd__a31oi_1", 4, 1); // A1,A2,A3,B1
    add_gate(gate_lib, "AOI32","sky130_fd_sc_hd__a32oi_1", 5, 1); // A1,A2,A3,B1,B2
    add_gate(gate_lib, "AOI41","sky130_fd_sc_hd__a41oi_1", 5, 1); // A1-4,B1

    // --- OAI family (OR-AND-INVERT) ---
    add_gate(gate_lib, "OAI21","sky130_fd_sc_hd__o21ai_1", 3, 1);
    add_gate(gate_lib, "OAI22","sky130_fd_sc_hd__o22ai_1", 4, 1);
    add_gate(gate_lib, "OAI31","sky130_fd_sc_hd__o31ai_1", 4, 1);
    add_gate(gate_lib, "OAI32","sky130_fd_sc_hd__o32ai_1", 5, 1);
    add_gate(gate_lib, "OAI41","sky130_fd_sc_hd__o41ai_1", 5, 1);

    // --- arithmetic building blocks ---
    add_gate(gate_lib, "HA",   "sky130_fd_sc_hd__ha_1",   2, 2); // A,B -> sum,cout
    add_gate(gate_lib, "FA",   "sky130_fd_sc_hd__fa_1",   3, 2); // A,B,CIN -> sum,cout
    add_gate(gate_lib, "MAJ3", "sky130_fd_sc_hd__maj3_1", 3, 1);
}

class GateNode : public std::enable_shared_from_this<GateNode> {
public:
    using NodePtr     = std::shared_ptr<GateNode>;
    using WeakNodePtr = std::weak_ptr<GateNode>;

    std::vector<wire_id> inputs;    // wire IDs feeding this gate (for netlist bookkeeping)
    std::vector<wire_id> outputs;   // wire IDs this gate drives

    std::string type;
    GateData data;

    // --- connectivity (populated after construction, see connect_input) ---
    std::vector<NodePtr> input_nodes;        // upstream gates (owned)
    std::vector<size_t>  input_pin;          // which output pin of input_nodes[i] feeds us
    std::vector<WeakNodePtr> output_nodes;   // downstream gates (non-owning)

    // --- primary-input support ---
    bool is_primary_input = false;
    bool primary_value = false;

    // --- evaluation state ---
    std::vector<bool> result_value;
    bool evaluated  = false;
    bool evaluating = false;   // cycle-detection guard

    GateNode(
        const std::vector<wire_id>& inputs_,
        const std::vector<wire_id>& outputs_,
        const std::string& type_
    ) : inputs(inputs_), outputs(outputs_), type(type_) {
        if (type == "INPUT") {
            is_primary_input = true;
            data = GateData{"INPUT", 0.0f, "INPUT", 0, 1};
            return;
        }
        auto it = gate_lib.find(type);
        if (it == gate_lib.end())
            throw std::invalid_argument("GateNode: unknown gate type '" + type + "'");
        data = it->second;

        if (inputs.size() != static_cast<size_t>(data.num_inputs))
            throw std::invalid_argument(
                "GateNode: '" + type + "' expects " + std::to_string(data.num_inputs) +
                " inputs, got " + std::to_string(inputs.size()));
        if (outputs.size() != static_cast<size_t>(data.num_outputs))
            throw std::invalid_argument(
                "GateNode: '" + type + "' expects " + std::to_string(data.num_outputs) +
                " outputs, got " + std::to_string(outputs.size()));
    }

    // Wire up one input pin (index `pin_index` in `inputs`) to a driver gate's
    // output pin `driver_pin`. Call once per input pin, in pin order.
    void connect_input(const NodePtr& driver, size_t driver_pin) {
        if (driver_pin >= driver->data.num_outputs)
            throw std::invalid_argument("connect_input: driver_pin out of range for '" + driver->type + "'");
        input_nodes.push_back(driver);
        input_pin.push_back(driver_pin);
        driver->output_nodes.push_back(weak_from_this());
    }

    // For primary inputs: set the value this node feeds forward.
    void set_primary_value(bool v) {
        if (!is_primary_input)
            throw std::logic_error("set_primary_value called on non-input gate '" + type + "'");
        primary_value = v;
    }

    // Clears memoized results so the same DAG can be re-evaluated with new
    // primary input values (needed every CGP fitness-eval call).
    void reset() {
        evaluated = false;
        evaluating = false;
        result_value.clear();
    }

    std::vector<bool> evaluate() {
        if (evaluated) return result_value;

        if (is_primary_input) {
            result_value = {primary_value};
            evaluated = true;
            return result_value;
        }

        if (evaluating)
            throw std::runtime_error("GateNode::evaluate: cycle detected at gate of type '" + type + "'");
        evaluating = true;

        std::vector<bool> input_values;
        input_values.reserve(input_nodes.size());
        for (size_t i = 0; i < input_nodes.size(); ++i) {
            const std::vector<bool>& upstream = input_nodes[i]->evaluate();
            size_t pin = input_pin[i];
            if (pin >= upstream.size())
                throw std::runtime_error("GateNode::evaluate: pin index out of range from '" + input_nodes[i]->type + "'");
            input_values.push_back(upstream[pin]);
        }

        result_value = data.evaluate(input_values);
        evaluating = false;
        evaluated = true;
        return result_value;
    }
};

class NetStruct {
public:
    std::vector<GateNode::NodePtr> nodes;
    std::vector<GateNode::NodePtr> input_nodes;
    std::vector<GateNode::NodePtr> output_nodes;
};

#endif // ATLAS_UTILS_H