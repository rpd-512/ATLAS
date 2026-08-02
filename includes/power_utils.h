#ifndef POWER_UTILS_H
#define POWER_UTILS_H

inline double compute_static_power(const Circuit& circuit,
                                  std::vector<std::string>* missing_out = nullptr) {
    double total = 0.0;
    for (const auto& g : circuit.gates) {
        if (g.data.leakage_power < 0.0) {
            if (missing_out) missing_out->push_back(g.id);
            continue;
        }
        total += g.data.leakage_power * 1e-9;   // nW -> W
    }
    return total;
}

// Total dynamic switching power across the circuit:
//   P_switching = sum over all gate outputs of  toggle_rate * 0.5 * C_load * V_dd^2 * f_clk
//
// Requires evaluate_circuit_soft() to have already been run on this circuit
// (so GateData::toggle_rate is populated) and output_capacitance to have
// been resolved (same C_load used by the STA delay/slew NLDM lookups).
//
// Units: output_capacitance is in fF (1e-15 F), nom_voltage in V, f_clk in Hz.
// Returns power in Watts.
inline float compute_switching_power(const Circuit& circuit, std::vector<std::string>* missing = nullptr) {
    constexpr float PF_TO_F = 1e-12f;

    const float v_dd = circuit.nom_voltage;
    const float f_clk = circuit.f_clk;

    float total_power = 0.0f;

    for (const Gate& g : circuit.gates) {
        if (g.data.toggle_rate.empty()) {
            if (missing) missing->push_back(g.id);
            continue;
        }

        if (g.data.output_capacitance < 0.0f) {
            if (missing) missing->push_back(g.id);
            continue;
        }

        const float c_load = g.data.output_capacitance * PF_TO_F;   // fF -> F

        // toggle_rate is parallel to the gate's output pins (same
        // convention as compiled_soft_fns / liberty->output_names). Sum
        // switching power across all outputs of this gate.
        for (float tr : g.data.toggle_rate) {
            total_power += tr * 0.5f * c_load * v_dd * v_dd * f_clk;
        }
    }

    return total_power;
}

// Internal (short-circuit) power: for the arc feeding each gate output,
// look up energy/switch from the fitted internal_power surface at
// (input_transition, output_capacitance) -- same two axes STA's delay/slew
// NLDM lookups use -- then scale by that output's toggle_rate and f_clk.
//
//   P_internal = energy_per_switch(t_in, C_load) * toggle_rate * f_clk
//
// Mirrors run_sta's own convention: the "critical" input pin (the one with
// the largest fitted delay, i.e. the same pin STA already picked as
// worst_delay) is used as the switching-energy source for that output --
// consistent with the idea that internal power is dominated by whichever
// input arc actually determines the output transition.
//
// Requires run_sta() (for input_transition[]/output_capacitance) and
// evaluate_circuit_soft() (for toggle_rate) to have both already run.
//
// Units: power_coeffs are fit directly from the Liberty internal_power
// table's raw numbers with no conversion applied anywhere in parse_liberty
// (same as capacitance/leakage) -- ENERGY_UNIT below must match whatever
// this library's `energy_unit` (or default nJ, per the specified format
// for internal_power tables) header field says; verify before trusting
// the output the same way capacitive_load_unit/leakage_power_unit were
// verified earlier.
inline float compute_internal_power(const Circuit& circuit, std::vector<std::string>* missing = nullptr) {
    constexpr float ENERGY_UNIT_TO_J = 1e-12f;   // pJ -> J; CONFIRM against this .lib's energy_unit header field

    const float f_clk = circuit.f_clk;
    float total_power = 0.0f;

    for (const Gate& g : circuit.gates) {
        if (!g.data.liberty) {
            if (missing) missing->push_back(g.id);
            continue;
        }
        if (g.data.toggle_rate.empty()) {
            if (missing) missing->push_back(g.id);
            continue;
        }
        if (g.data.output_capacitance < 0.0f) {
            if (missing) missing->push_back(g.id);
            continue;
        }

        const LibertyCellData& lib = *g.data.liberty;
        const float out_cap = g.data.output_capacitance;

        size_t num_arcs = std::min(g.inputs.size(), lib.pin_arcs.size());
        if (lib.num_pins > 0) {
            num_arcs = std::min(num_arcs, static_cast<size_t>(lib.num_pins));
        }
        if (num_arcs == 0) {
            if (missing) missing->push_back(g.id);
            continue;
        }

        // Pick the same "critical" pin STA's delay pass would pick: the
        // input arc with the largest fitted propagation delay at this
        // gate's actual (input_transition, output_capacitance) operating
        // point, then read *that* pin's energy/switch off power_coeffs.
        float worst_delay = -std::numeric_limits<float>::infinity();
        float energy_per_switch = 0.0f;

        for (size_t k = 0; k < num_arcs; ++k) {
            float t_in = (k < g.data.input_transition.size())
                             ? g.data.input_transition[k]
                             : 0.0f;
            const PinTimingArc& arc = lib.pin_arcs[k];

            float d = eval_nldm(arc.propagation_coeffs, t_in, out_cap,
                                 arc.min_transition, arc.max_transition,
                                 arc.min_load, arc.max_load);
            if (d > worst_delay) {
                worst_delay = d;
                energy_per_switch = eval_nldm(arc.power_coeffs, t_in, out_cap,
                                               arc.min_transition, arc.max_transition,
                                               arc.min_load, arc.max_load);
            }
        }

        const float energy_j = energy_per_switch * ENERGY_UNIT_TO_J;

        // toggle_rate is parallel to this gate's output pins (same
        // convention compute_switching_power already uses).
        for (float tr : g.data.toggle_rate) {
            total_power += energy_j * tr * f_clk;
        }
    }

    return total_power;
}

#endif // POWER_UTILS_H