#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

inline void print_liberty_library(const LibertyLibrary& liberty) {
    for (const auto& [name, entry] : liberty.cells_library) {
        std::cout << name << "\n";

        std::cout << "  -area: " << entry.area << "\n";

        std::cout << "  -input_capacitances: ";
        for (size_t i = 0; i < entry.input_capacitances.size(); ++i) {
            std::cout << entry.input_capacitances[i];
            if (i + 1 < entry.input_capacitances.size()) std::cout << ", ";
        }
        std::cout << "\n";

        std::cout << "  -num_pins: " << entry.num_pins << "\n";

        for (int p = 0; p < entry.num_pins; ++p) {
            const PinTimingArc& arc = entry.pin_arcs[p];
            std::cout << "  -pin[" << p << "]\n";

            std::cout << "    -propagation_coeffs: ";
            for (size_t i = 0; i < arc.propagation_coeffs.size(); ++i) {
                std::cout << arc.propagation_coeffs[i];
                if (i + 1 < arc.propagation_coeffs.size()) std::cout << ", ";
            }
            std::cout << "\n";

            std::cout << "    -slew_coeffs: ";
            for (size_t i = 0; i < arc.slew_coeffs.size(); ++i) {
                std::cout << arc.slew_coeffs[i];
                if (i + 1 < arc.slew_coeffs.size()) std::cout << ", ";
            }
            std::cout << "\n";

            std::cout << "    -min_transition: " << arc.min_transition << "\n";
            std::cout << "    -max_transition: " << arc.max_transition << "\n";
            std::cout << "    -min_load: " << arc.min_load << "\n";
            std::cout << "    -max_load: " << arc.max_load << "\n";
        }

        std::cout << "\n";
    }
    std::cout << "Liberty Library: " << liberty.nom_voltage << "V\n";
}

// Prints, per gate, all the quantities feeding into switching power (and
// the adjacent area/timing/static-power fields) so you can spot where a
// near-zero contributes to the total: toggle_rate per output pin,
// output_capacitance (C_load), computed switching power for that gate,
// plus area/leakage/AT/RT/slack for context. Call after both
// evaluate_circuit_soft() and STA have run (same prerequisites as
// compute_switching_power).
inline void debug_print_gate_power(const Circuit& circuit) {
    constexpr float FF_TO_F = 1e-15f;
    const float v_dd = circuit.nom_voltage;
    const float f_clk = circuit.f_clk;

    std::cout << "\n--- Per-gate power debug dump ---\n";
    std::cout << "circuit.nom_voltage = " << v_dd << " V, circuit.f_clk = " << f_clk << " Hz\n\n";

    float total_switching = 0.0f;
    int gates_missing_toggle = 0;
    int gates_missing_cap = 0;

    for (const Gate& g : circuit.gates) {
        std::cout << g.id << " (" << g.data.type << "):\n";
        std::cout << "\tarea             = " << g.data.area << " um^2\n";
        std::cout << "\tleakage_power    = " << g.data.leakage_power << " uW\n";
        std::cout << "\toutput_capacitance = " << g.data.output_capacitance << " fF\n";
        std::cout << "\toutput_transition  = " << g.data.output_transition << " ns\n";
        std::cout << "\tdelay            = " << g.data.delay << " ns\n";
        std::cout << "\tarrival_time     = " << g.data.arrival_time << " ns\n";
        std::cout << "\trequired_time    = " << g.data.required_time << " ns\n";
        std::cout << "\tslack            = " << g.data.slack << " ns\n";

        if (g.data.toggle_rate.empty()) {
            std::cout << "\ttoggle_rate      = <empty -- evaluate_circuit_soft not run or gate unreachable>\n";
            ++gates_missing_toggle;
        } else {
            std::cout << "\ttoggle_rate      = [";
            for (size_t i = 0; i < g.data.toggle_rate.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << g.data.toggle_rate[i];
            }
            std::cout << "]\n";
        }

        if (g.data.output_capacitance < 0.0f) {
            std::cout << "\t-> output_capacitance not resolved (<0), skipped for power sum\n";
            ++gates_missing_cap;
        }

        if (!g.data.toggle_rate.empty() && g.data.output_capacitance >= 0.0f) {
            const float c_load = g.data.output_capacitance * FF_TO_F;
            float gate_power = 0.0f;
            for (float tr : g.data.toggle_rate) {
                gate_power += tr * 0.5f * c_load * v_dd * v_dd * f_clk;
            }
            std::cout << "\tswitching_power  = " << gate_power << " W\n";
            total_switching += gate_power;
        } else {
            std::cout << "\tswitching_power  = <skipped, missing toggle_rate and/or output_capacitance>\n";
        }

        std::cout << "\n";
    }

    std::cout << "--- Summary ---\n";
    std::cout << "total gates:                " << circuit.gates.size() << "\n";
    std::cout << "gates missing toggle_rate:  " << gates_missing_toggle << "\n";
    std::cout << "gates missing output_cap:   " << gates_missing_cap << "\n";
    std::cout << "sum of per-gate switching power: " << total_switching << " W\n";
    std::cout << "----------------------------------\n\n";
}

#endif // DEBUG_UTILS_H