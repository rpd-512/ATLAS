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

class GateNode{
public:
    std::vector<wire_id> inputs;
    std::vector<wire_id> outputs;
    std::vector<bool> result_value;

    bool evaluated = false;

    std::string type;
    GateData data;
    
    GateNode(
        const std::vector<wire_id>& inputs,
        const std::vector<wire_id>& outputs,
        const std::string& type
    ) : inputs(inputs), outputs(outputs), type(type) {
        data = gate_lib[type];
    };
    std::vector<wire_id> evaluate();
};


struct NetStruct{
    std::vector<GateNode> nodes;
    std::vector<GateNode> input_nodes;
    std::vector<GateNode> output_nodes;
};

#endif // ATLAS_UTILS_H