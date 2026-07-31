#ifndef AREA_UTILS_H
#define AREA_UTILS_H

#include "types.h"

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

// Populates GateData::area on every gate in circuit by looking up
// gate.data.name (the raw, unstripped cell type) in the liberty library.
// Gates with no matching liberty entry are left at their default (-1.0);
// warn_missing controls whether that's reported to stderr.
inline void attach_liberty_data(Circuit& circuit, const LibertyLibrary& liberty,
                                 bool warn_missing = true) {
    for (Gate& g : circuit.gates) {
        auto it = liberty.find(g.data.name);
        if (it != liberty.end()) {
            g.data.area = it->second.area;
        } else if (warn_missing) {
            std::cerr << "attach_liberty_data: no liberty entry for cell type '"
                       << g.data.name << "' (gate '" << g.id << "')\n";
        }
    }
}



#endif // AREA_UTILS_H