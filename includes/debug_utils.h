#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

inline void fill_liberty_test_defaults(LibertyLibrary& liberty) {
    for (auto& [name, entry] : liberty) {
        if (entry.propagation_coeffs[0] != 0.0f || entry.propagation_coeffs[1] != 0.0f) continue; // already has real data

        entry.propagation_coeffs = {
            0.0121f, 0.1850f, 12.430f, 0.9200f, -0.0043f, 3.1100f
        };
        entry.slew_coeffs = {
            0.0089f, 0.6400f, 9.870f, 0.3300f, 0.0011f, 1.9800f
        };
    }
}

inline void print_liberty_library(const LibertyLibrary& liberty) {
    for (const auto& [name, entry] : liberty) {
        std::cout << name << "\n";

        std::cout << "  -area: " << entry.area << "\n";

        std::cout << "  -input_capacitances: ";
        for (size_t i = 0; i < entry.input_capacitances.size(); ++i) {
            std::cout << entry.input_capacitances[i];
            if (i + 1 < entry.input_capacitances.size()) std::cout << ", ";
        }
        std::cout << "\n";

        std::cout << "  -propagation_coeffs: ";
        for (size_t i = 0; i < entry.propagation_coeffs.size(); ++i) {
            std::cout << entry.propagation_coeffs[i];
            if (i + 1 < entry.propagation_coeffs.size()) std::cout << ", ";
        }
        std::cout << "\n";

        std::cout << "  -slew_coeffs: ";
        for (size_t i = 0; i < entry.slew_coeffs.size(); ++i) {
            std::cout << entry.slew_coeffs[i];
            if (i + 1 < entry.slew_coeffs.size()) std::cout << ", ";
        }
        std::cout << "\n";

        std::cout << "  -min_transition: " << entry.min_transition << "\n";
        std::cout << "  -max_transition: " << entry.max_transition << "\n";
        std::cout << "  -min_load: " << entry.min_load << "\n";
        std::cout << "  -max_load: " << entry.max_load << "\n";

        std::cout << "\n";
    }
}

#endif // DEBUG_UTILS_H