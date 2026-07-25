#ifndef ATLAS_UTILS_H
#define ATLAS_UTILS_H

#include <string>
#include <unordered_map>
#include <optional>
#include <stdexcept>

struct Metrics {
    // --- Design identification (for reproducibility) ---
    std::string design_name;       // top module name
    std::string source_file;       // path to the .sv file synthesized
    std::string liberty_file;      // path to the .lib used
    std::string technology;        // PDK/technology node name (e.g. "sky130_fd_sc_hd")
    std::string pvt_corner;        // e.g. "tt_025C_1v80" - critical for interpreting area/timing/power
    std::string yosys_version;     // e.g. "0.67+42 (6f7123d1d-dirty)"
    std::string synth_command;     // exact -p script used, for debugging/reproducing later

    // --- Area (µm², from yosys `stat -liberty -json`) ---
    double area = 0.0;                   // total area (combinational + sequential)
    double sequential_area = 0.0;        // area attributable to sequential cells
    double combinational_area = 0.0;     // derived: area - sequential_area

    // --- Timing (requires OpenROAD/OpenSTA + SDC constraint) ---
    std::optional<double> critical_path;   // ns
    std::optional<double> worst_slack;     // ns

    // --- Power (requires OpenROAD/OpenSTA) ---
    std::optional<double> internal_power;   // W
    std::optional<double> switching_power;  // W
    std::optional<double> leakage_power;    // W
    std::optional<double> total_power;      // W

    // --- Cell/gate statistics ---
    int total_cells = 0;
    int combinational_cells = 0;
    int sequential_cells = 0;
    std::unordered_map<std::string, int> cell_count;  // e.g. "sky130_fd_sc_hd__nor2_1" -> 1

    // --- Structural statistics (from yosys `stat -json`) ---
    int num_wires = 0;
    int num_wire_bits = 0;
    int num_pub_wires = 0;
    int num_pub_wire_bits = 0;
    int num_ports = 0;
    int num_port_bits = 0;
    int num_memories = 0;
    int num_memory_bits = 0;
    int num_submodules = 0;

    // --- Runtime (wall-clock, measured by Atlas, not yosys's internal pass timing) ---
    double yosys_runtime = 0.0;
    double openroad_runtime = 0.0;
    double total_runtime = 0.0;
};

#include "io_utils.h"


#endif // ATLAS_UTILS_H