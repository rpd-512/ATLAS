#ifndef STA_UTILS_H
#define STA_UTILS_H

#include <queue>
#include <functional>
#include <limits>
#include "types.h"

// Topologically walks the circuit, assigning input_transition[], output_capacitance,
// output_transition, and delay on every GateData in place.
inline void run_sta(Circuit& circuit, float default_input_transition = 0.01f) {
    std::unordered_map<wire_id, std::pair<size_t, size_t>> driver_of;
    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        const auto& outs = circuit.gates[gi].outputs;
        for (size_t p = 0; p < outs.size(); ++p) driver_of[outs[p]] = {gi, p};
    }

    // structural fanout cap per wire: sum of input_capacitances[pin] over every
    // gate input tied to that wire. Order-independent, doesn't need the DAG walk.
    std::unordered_map<wire_id, float> fanout_cap;
    for (const Gate& g : circuit.gates) {
        for (size_t p = 0; p < g.inputs.size(); ++p) {
            float cap = (p < g.data.input_capacitances.size()) ? g.data.input_capacitances[p] : 0.0f;
            fanout_cap[g.inputs[p]] += cap;
        }
    }
    // primary outputs currently see no external load beyond gate fanout (no PO pin/pad cap model yet)

    std::vector<char> done(circuit.gates.size(), false);
    std::vector<char> visiting(circuit.gates.size(), false);

    std::function<float(wire_id)> transition_of = [&](wire_id w) -> float {
        if (w == WIRE_CONST_0 || w == WIRE_CONST_1) return 0.0f;

        auto drv = driver_of.find(w);
        if (drv == driver_of.end()) return default_input_transition;   // primary input, no driver

        size_t gidx = drv->second.first;
        Gate& g = circuit.gates[gidx];

        if (!done[gidx]) {
            if (visiting[gidx]) {
                throw std::runtime_error("run_sta: combinational loop through gate '" + g.id + "'");
            }
            visiting[gidx] = true;

            // input_transition[k] = transition arriving on pin k; max over all pins
            // feeds the delay/slew surface, per-pin values still individually stored
            float max_in_trans = 0.0f;
            for (size_t k = 0; k < g.inputs.size(); ++k) {
                float t = transition_of(g.inputs[k]);
                if (k < g.data.input_transition.size()) g.data.input_transition[k] = t;
                max_in_trans = std::max(max_in_trans, t);
            }

            // output_capacitance = sum of fanout pin caps across every output pin
            // this gate drives (ha/fa have 2; everything else has 1)
            float out_cap = 0.0f;
            for (wire_id ow : g.outputs) {
                auto it = fanout_cap.find(ow);
                out_cap += (it != fanout_cap.end()) ? it->second : 0.0f;
            }
            g.data.output_capacitance = out_cap;

            if (!g.data.liberty) {
                throw std::runtime_error("run_sta: gate '" + g.id + "' has no liberty data attached");
            }
            const LibertyCellData& lib = *g.data.liberty;

            g.data.delay = eval_nldm(lib.propagation_coeffs, max_in_trans, out_cap,
                                      lib.min_transition, lib.max_transition,
                                      lib.min_load, lib.max_load);
            g.data.output_transition = eval_nldm(lib.slew_coeffs, max_in_trans, out_cap,
                                                  lib.min_transition, lib.max_transition,
                                                  lib.min_load, lib.max_load);

            visiting[gidx] = false;
            done[gidx] = true;
        }

        return g.data.output_transition;
    };

    // drive from every gate output, not just circuit.outputs, so dead-end/unobserved
    // gates still get their fields populated
    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        if (done[gi]) continue;
        for (wire_id ow : circuit.gates[gi].outputs) transition_of(ow);
    }
}






inline void run_sta_arrival(Circuit& circuit) {
    std::unordered_map<wire_id, std::pair<size_t, size_t>> driver_of;
    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        const auto& outs = circuit.gates[gi].outputs;
        for (size_t p = 0; p < outs.size(); ++p) driver_of[outs[p]] = {gi, p};
    }

    for (const Gate& g : circuit.gates) {
        if (g.data.delay < 0.0f) {
            throw std::runtime_error("run_sta_arrival: gate '" + g.id +
                                      "' has no delay (run_sta must run first)");
        }
    }

    // wire -> gates that consume it as an input, one entry per occurrence
    // (a gate using the same wire on two pins counts twice, matching how
    // many times `pending` needs to be decremented for that gate).
    std::unordered_map<wire_id, std::vector<size_t>> consumers_of;
    std::vector<int> pending(circuit.gates.size(), 0);

    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        const Gate& g = circuit.gates[gi];
        for (wire_id iw : g.inputs) {
            if (iw == WIRE_CONST_0 || iw == WIRE_CONST_1) continue;
            if (driver_of.find(iw) == driver_of.end()) continue;  // primary input: always ready
            pending[gi]++;
            consumers_of[iw].push_back(gi);
        }
    }

    std::unordered_map<wire_id, float> wire_arrival;  // filled in as gates finalize
    auto input_arrival = [&](wire_id w) -> float {
        if (w == WIRE_CONST_0 || w == WIRE_CONST_1) return 0.0f;
        auto it = wire_arrival.find(w);
        return (it != wire_arrival.end()) ? it->second : 0.0f;  // primary input: t=0
    };

    // std::pair's default operator< compares first-then-second, and
    // std::priority_queue is a max-heap by default -- no comparator needed
    // for the "max" side of this.
    std::priority_queue<std::pair<float, size_t>> ready;

    auto compute_arrival = [&](size_t gi) -> float {
        const Gate& g = circuit.gates[gi];
        float max_in = 0.0f;
        for (wire_id iw : g.inputs) max_in = std::max(max_in, input_arrival(iw));
        return max_in + g.data.delay;
    };

    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        if (pending[gi] == 0) ready.push({compute_arrival(gi), gi});
    }

    std::vector<char> finalized(circuit.gates.size(), false);
    size_t finalized_count = 0;

    while (!ready.empty()) {
        auto [arrival, gi] = ready.top();
        ready.pop();
        if (finalized[gi]) continue;  // safe even though degree-gating means each gate is pushed exactly once
        finalized[gi] = true;
        ++finalized_count;

        Gate& g = circuit.gates[gi];
        g.data.arrival_time = arrival;

        for (wire_id ow : g.outputs) {
            wire_arrival[ow] = arrival;  // ha/fa: both output pins share this gate's single arrival_time

            auto cit = consumers_of.find(ow);
            if (cit == consumers_of.end()) continue;
            for (size_t consumer : cit->second) {
                if (--pending[consumer] == 0) ready.push({compute_arrival(consumer), consumer});
            }
        }
    }

    if (finalized_count != circuit.gates.size()) {
        throw std::runtime_error("run_sta_arrival: combinational loop detected (not all gates finalized)");
    }
}

// ----------------------------------------------------------------------
// Backward pass: required_time[gate] = min over every consumer c of
// (required_time[c] - delay[c]), seeded at primary outputs with
// `target_period`. Same degree-gating soundness argument as above, mirrored:
// a gate is only finalized once every one of its consumers already has a
// final required_time, so nothing arrives after the fact to invalidate it.
// std::greater<> flips priority_queue's default max-heap into a min-heap.
//
// Must run after run_sta_arrival(): if target_period < 0, defaults to the
// worst (max) arrival time seen at any primary output -- i.e. a zero-worst-
// slack constraint, since there's no SDC-style external clock period
// anywhere else in this codebase.
// ----------------------------------------------------------------------
inline void run_sta_required(Circuit& circuit, float target_period = -1.0f) {
    std::unordered_map<wire_id, std::pair<size_t, size_t>> driver_of;
    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        const auto& outs = circuit.gates[gi].outputs;
        for (size_t p = 0; p < outs.size(); ++p) driver_of[outs[p]] = {gi, p};
    }

    for (const Gate& g : circuit.gates) {
        if (g.data.arrival_time < 0.0f) {
            throw std::runtime_error("run_sta_required: arrival_time not set for gate '" + g.id +
                                      "' (run_sta_arrival must run first)");
        }
        if (g.data.delay < 0.0f) {
            throw std::runtime_error("run_sta_required: gate '" + g.id +
                                      "' has no delay (run_sta must run first)");
        }
    }

    if (target_period < 0.0f) {
        float worst = 0.0f;
        for (wire_id ow : circuit.outputs) {
            auto d = driver_of.find(ow);
            float a = (d != driver_of.end()) ? circuit.gates[d->second.first].data.arrival_time : 0.0f;
            worst = std::max(worst, a);
        }
        target_period = worst;
    }

    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> required_acc(circuit.gates.size(), INF);
    std::vector<int> out_pending(circuit.gates.size(), 0);

    // seed: gates driving a primary output get the external target directly;
    // internal fanout edges become pending obligations resolved as each
    // consumer's required_time gets finalized.
    for (wire_id ow : circuit.outputs) {
        auto d = driver_of.find(ow);
        if (d == driver_of.end()) continue;  // PO tied straight to a primary input/const, no gate to constrain
        required_acc[d->second.first] = std::min(required_acc[d->second.first], target_period);
    }
    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        for (wire_id iw : circuit.gates[gi].inputs) {
            if (iw == WIRE_CONST_0 || iw == WIRE_CONST_1) continue;
            auto d = driver_of.find(iw);
            if (d == driver_of.end()) continue;
            out_pending[d->second.first]++;
        }
    }

    std::priority_queue<std::pair<float, size_t>, std::vector<std::pair<float, size_t>>, std::greater<>> ready;
    for (size_t gi = 0; gi < circuit.gates.size(); ++gi) {
        if (out_pending[gi] == 0) ready.push({required_acc[gi], gi});
    }

    std::vector<char> finalized(circuit.gates.size(), false);
    size_t finalized_count = 0;

    while (!ready.empty()) {
        auto [req, gi] = ready.top();
        ready.pop();
        if (finalized[gi]) continue;
        finalized[gi] = true;
        ++finalized_count;

        Gate& g = circuit.gates[gi];
        g.data.required_time = req;

        for (wire_id iw : g.inputs) {
            if (iw == WIRE_CONST_0 || iw == WIRE_CONST_1) continue;
            auto d = driver_of.find(iw);
            if (d == driver_of.end()) continue;
            size_t pgi = d->second.first;
            required_acc[pgi] = std::min(required_acc[pgi], req - g.data.delay);
            if (--out_pending[pgi] == 0) ready.push({required_acc[pgi], pgi});
        }
    }

    if (finalized_count != circuit.gates.size()) {
        throw std::runtime_error("run_sta_required: combinational loop detected (not all gates finalized)");
    }
}

// ----------------------------------------------------------------------
// Final pass: slack[gate] = required_time[gate] - arrival_time[gate].
// Must run after both run_sta_arrival() and run_sta_required().
// ----------------------------------------------------------------------
inline void run_sta_slack(Circuit& circuit) {
    for (Gate& g : circuit.gates) {
        if (g.data.arrival_time < 0.0f) {
            throw std::runtime_error("run_sta_slack: arrival_time not set for gate '" + g.id +
                                      "' (run_sta_arrival must run first)");
        }
        g.data.slack = g.data.required_time - g.data.arrival_time;
    }
}

#endif // STA_UTILS_H