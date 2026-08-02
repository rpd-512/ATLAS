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
        total += g.data.leakage_power;
    }
    return total;
}

#endif // POWER_UTILS_H