#ifndef ATLAS_UTILS_H
#define ATLAS_UTILS_H

#include "io_utils.h"

inline SignalArray evaluate_circuit(const Circuit& circuit, const SignalArray& input_values) {
    if (input_values.size() != circuit.inputs.size()) {
        throw std::runtime_error("evaluate_circuit: input_values size (" + std::to_string(input_values.size()) +
                                  ") does not match circuit.inputs size (" + std::to_string(circuit.inputs.size()) + ")");
    }

    std::unordered_map<wire_id, bool> input_map;
    input_map.reserve(circuit.inputs.size());
    for (size_t i = 0; i < circuit.inputs.size(); ++i) {
        input_map[circuit.inputs[i]] = input_values[i];
    }

    // wire -> (gate index, output pin index within that gate)
    std::unordered_map<wire_id, std::pair<size_t, size_t>> driver_of;
    for (size_t g = 0; g < circuit.gates.size(); ++g) {
        const auto& outs = circuit.gates[g].outputs;
        for (size_t p = 0; p < outs.size(); ++p) {
            driver_of[outs[p]] = {g, p};
        }
    }

    std::vector<bool> node_done(circuit.gates.size(), false);
    std::vector<char> node_visiting(circuit.gates.size(), false);   // combinational-loop guard
    std::vector<std::vector<bool>> node_result(circuit.gates.size());

    std::function<bool(wire_id)> value_of = [&](wire_id w) -> bool {
        if (w == WIRE_CONST_0) return false;
        if (w == WIRE_CONST_1) return true;

        auto in_it = input_map.find(w);
        if (in_it != input_map.end()) return in_it->second;

        auto drv_it = driver_of.find(w);
        if (drv_it == driver_of.end()) {
            throw std::runtime_error("evaluate_circuit: wire " + std::to_string(w) +
                                      " is neither a primary input nor any gate's output");
        }

        size_t gidx = drv_it->second.first;
        size_t pidx = drv_it->second.second;

        if (!node_done[gidx]) {
            if (node_visiting[gidx]) {
                throw std::runtime_error("evaluate_circuit: combinational loop through gate '" +
                                          circuit.gates[gidx].id + "'");
            }
            node_visiting[gidx] = true;

            const Gate& g = circuit.gates[gidx];
            std::vector<bool> args;
            args.reserve(g.inputs.size());
            for (wire_id in_w : g.inputs) {
                args.push_back(value_of(in_w));
            }
            node_result[gidx] = g.data.evaluate(args);

            node_visiting[gidx] = false;
            node_done[gidx] = true;
        }

        return node_result[gidx].at(pidx);
    };

    SignalArray result;
    result.reserve(circuit.outputs.size());
    for (wire_id w : circuit.outputs) {
        result.push_back(value_of(w));
    }
    return result;
}

inline double compute_total_area(const Circuit& circuit,
                                  std::vector<std::string>* missing_out = nullptr) {
    double total = 0.0;
    for (const auto& g : circuit.gates) {
        if (g.data.area < 0.0) {
            if (missing_out) missing_out->push_back(g.id);
            continue;
        }
        total += g.data.area;
    }
    return total;
}

#endif // ATLAS_UTILS_H