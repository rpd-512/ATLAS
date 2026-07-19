#ifndef OPENROAD_UTILS_H
#define OPENROAD_UTILS_H

#include <chrono>

#include "atlas_utils.h"  // for Metrics

// Stub for now — timing (critical_path, worst_slack) and power
// (internal_power, switching_power, leakage_power, total_power)
// require running OpenROAD/OpenSTA against the synthesized netlist
// plus an SDC clock constraint. Left as std::nullopt until wired up.
inline void analyse_openroad_impl(Metrics& metrics) {
    auto start_time = std::chrono::steady_clock::now();

    // TODO: invoke OpenROAD here, parse timing/power reports,
    // populate metrics.critical_path, metrics.worst_slack,
    // metrics.internal_power, metrics.switching_power,
    // metrics.leakage_power, metrics.total_power.

    auto end_time = std::chrono::steady_clock::now();
    metrics.openroad_runtime =
        std::chrono::duration<double>(end_time - start_time).count();
}

#endif // OPENROAD_UTILS_H