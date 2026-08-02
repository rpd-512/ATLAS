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

#endif // DEBUG_UTILS_H