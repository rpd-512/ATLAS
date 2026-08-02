#ifndef TYPES_H
#define TYPES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <regex>
#include <nlohmann/json.hpp>

#include <Eigen/Dense>

#define MAX_COEFF 10

using wire_id = uint32_t;
using SignalArray = std::vector<bool>;

constexpr wire_id WIRE_CONST_0 = std::numeric_limits<wire_id>::max() - 1;
constexpr wire_id WIRE_CONST_1 = std::numeric_limits<wire_id>::max();

constexpr int MAX_ARITY = 5; // a221o

struct PinTimingArc {
    // fitted quadratic surface: delay(x,y) = c0 + c1*x + c2*y + c3*x*y + c4*x^2 + c5*y^2
    std::array<float, MAX_COEFF> propagation_coeffs{};   // fit from collapsed propagation table
    std::array<float, MAX_COEFF> slew_coeffs{};          // fit from collapsed slew table
    std::array<float, MAX_COEFF> power_coeffs{};          // fit from collapsed slew table

    // keep bounds so you can clamp instead of blindly extrapolate
    float min_transition = 0.0f, max_transition = 0.0f;
    float min_load = 0.0f, max_load = 0.0f;
};

struct LibertyCellData {
    std::string name;
    double area = 0.0;
    double leakage_power = 0.0;

    // input pin name -> dense index (0..num_pins-1), assigned in
    // parse_liberty's pass B in file order -- same index space used by
    // input_capacitances/pin_arcs below.
    std::unordered_map<std::string, int> pin_index;

    // one boolean function string per output pin (Liberty's `function`
    // attribute), parallel to output_names, in output-pin file order.
    // e.g. output_names[0] = "Y", function_strings[0] = "(A&!B&!C)|(!A&B&!C)".
    std::vector<std::string> output_names;
    std::vector<std::string> function_strings;

    std::array<float, MAX_ARITY> input_capacitances{};

    // one timing arc (delay + slew surfaces) per input pin
    std::array<PinTimingArc, MAX_ARITY> pin_arcs{};

    // number of pins actually populated (arrays are fixed-size for cache friendliness)
    int num_pins = 0;
};

// ----------------------------------------------------------------------
// Phenotype circuit representation (built by io_utils.h's parse_netlist,
// consumed by atlas_utils.h's evaluate_circuit).
// ----------------------------------------------------------------------
struct GateData {
    std::string name;      // raw cell type, e.g. "sky130_fd_sc_hd__nand2_1"
    std::string type;      // stripped, e.g. "nand2" -- descriptive only, no longer a dispatch key

    // compiled from liberty->function_strings via attach_liberty_data (see
    // eval_utils.h's compile_boolean_expr); one entry per output, parallel
    // to liberty->output_names.
    std::vector<std::function<bool(const std::unordered_map<std::string, bool>&)>> compiled_fns;

    // continuous/relaxed counterpart of compiled_fns, compiled from the
    // same liberty->function_strings via soft_eval_utils.h's
    // compile_soft_expr (product t-norm). Populated alongside compiled_fns
    // wherever liberty data is attached -- same size and pin ordering.
    std::vector<std::function<float(const std::unordered_map<std::string, float>&)>> compiled_soft_fns;

    std::vector<bool> evaluate(const std::vector<bool>& in) const {
        if (!liberty) {
            throw std::runtime_error(
                "GateData::evaluate: no liberty attached for gate type '" + name + "'");
        }
        if (compiled_fns.empty()) {
            throw std::runtime_error(
                "GateData::evaluate: no compiled function for gate '" + name + "'");
        }

        // Positional vector -> named map, via the cell's pin_index (name -> index).
        std::unordered_map<std::string, bool> vars;
        vars.reserve(liberty->pin_index.size());
        for (const auto& [pin_name, idx] : liberty->pin_index) {
            if (idx >= 0 && static_cast<size_t>(idx) < in.size()) {
                vars[pin_name] = in[idx];
            }
        }

        std::vector<bool> out;
        out.reserve(compiled_fns.size());
        for (const auto& fn : compiled_fns) out.push_back(fn(vars));
        return out;
    }

    // Same dispatch as evaluate(), but over the continuous [0,1] relaxation
    // (product t-norm: AND = A*B, OR = A+B-A*B, NOT = 1-A). Used for
    // differentiable / soft-CGP evaluation paths where gate inputs are
    // probabilities or relaxed logic values rather than hard booleans.
    std::vector<float> evaluate_soft(const std::vector<float>& in) const {
        if (!liberty) {
            throw std::runtime_error(
                "GateData::evaluate_soft: no liberty attached for gate type '" + name + "'");
        }
        if (compiled_soft_fns.empty()) {
            throw std::runtime_error(
                "GateData::evaluate_soft: no compiled soft function for gate '" + name + "'");
        }

        // Positional vector -> named map, via the cell's pin_index (name -> index).
        std::unordered_map<std::string, float> vars;
        vars.reserve(liberty->pin_index.size());
        for (const auto& [pin_name, idx] : liberty->pin_index) {
            if (idx >= 0 && static_cast<size_t>(idx) < in.size()) {
                vars[pin_name] = in[idx];
            }
        }

        std::vector<float> out;
        out.reserve(compiled_soft_fns.size());
        for (const auto& fn : compiled_soft_fns) out.push_back(fn(vars));
        return out;
    }

    float area = -1;              // area in um^2, for area-based fitness
    float leakage_power = -1;       // leakage power in uW, for power-based fitness

    std::vector<float> toggle_rate;

    std::array<float, MAX_ARITY> input_capacitances{};   // fF, per input pin
    std::array<float, MAX_ARITY> input_transition{};   // fF, per input pin

    const LibertyCellData* liberty = nullptr;   // non-owning; must outlive Circuit

    float output_capacitance = -1;   // fF — sum of fanout pin caps + wire cap
    float output_transition = -1;    // ns — resolved via NLDM lookup
    float delay = -1;                // ns — resolved worst-case arc delay
    float arrival_time = -1;         // ns — cached, forward topo pass
    float required_time = -1;        // ns — cached, backward pass
    float slack = -1;                // ns — required_time - arrival_time
};
struct Gate {
    std::string id;                 // cell instance name from the netlist
    std::vector<wire_id> outputs;   // this gate's output wires, in pin order
    GateData data;
    std::vector<wire_id> inputs;    // this gate's input wires, in pin order (may include WIRE_CONST_0/1)
};

struct Circuit {
    float f_clk = 1e9;
    float nom_voltage;
    std::vector<Gate> gates;
    std::vector<wire_id> inputs;    // primary input wires, in port-bit order
    std::vector<wire_id> outputs;   // primary output wires, in port-bit order
};


// Fits delay/slew = c0 + c1*x + c2*y + c3*x*y + c4*x^2 + c5*y^2
// over an NLDM table, via closed-form linear least squares (QR solve).
//
// index_1: input transition breakpoints (rows of table)
// index_2: output load breakpoints (columns of table)
// table:   table[i][j] corresponds to (index_1[i], index_2[j])
inline std::array<float, MAX_COEFF> fit_nldm_coeffs(
    const std::vector<float>& index_1,
    const std::vector<float>& index_2,
    const std::vector<std::vector<float>>& table)
{
    const size_t n = index_1.size();
    const size_t m = index_2.size();

    if (table.size() != n) {
        throw std::runtime_error("fit_nldm_coeffs: table row count (" +
            std::to_string(table.size()) + ") does not match index_1 size (" +
            std::to_string(n) + ")");
    }
    for (size_t i = 0; i < n; ++i) {
        if (table[i].size() != m) {
            throw std::runtime_error("fit_nldm_coeffs: table row " + std::to_string(i) +
                " has size " + std::to_string(table[i].size()) +
                ", expected " + std::to_string(m) + " (index_2 size)");
        }
    }

    const size_t rows = n * m;
    Eigen::MatrixXf A(rows, 6);
    Eigen::VectorXf z(rows);

    size_t r = 0;
    for (size_t i = 0; i < n; ++i) {
        const float x = index_1[i];
        for (size_t j = 0; j < m; ++j) {
            const float y = index_2[j];
            A(r, 0) = 1.0f;
            A(r, 1) = x;
            A(r, 2) = y;
            A(r, 3) = x * y;
            A(r, 4) = x * x;
            A(r, 5) = y * y;
            z(r) = table[i][j];
            ++r;
        }
    }

    Eigen::VectorXf solved = A.colPivHouseholderQr().solve(z);

    std::array<float, MAX_COEFF> coeffs{};
    for (int k = 0; k < 6; ++k) coeffs[k] = solved(k);
    return coeffs;
}


inline float eval_nldm(const std::array<float, MAX_COEFF>& c, float x, float y,
                        float x_min, float x_max, float y_min, float y_max)
{
    x = std::clamp(x, x_min, x_max);
    y = std::clamp(y, y_min, y_max);
    return c[0] + c[1]*x + c[2]*y + c[3]*x*y + c[4]*x*x + c[5]*y*y;
}

#endif // TYPES_H