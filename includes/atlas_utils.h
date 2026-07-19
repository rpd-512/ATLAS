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
#include "yosys_utils.h"
#include "openroad_utils.h"

class Atlas {
private:
    std::string file_path;
    std::string top_module;
    std::string liberty_path;
    std::string results;
    Metrics metrics;

public:
    Atlas(const std::string& file, const std::string& top, const std::string& liberty)
        : file_path(file), top_module(top), liberty_path(liberty) {
        if (!file_is_readable(file)) {
            throw std::runtime_error("Failed to open: " + file);
        }
        if (!file_is_readable(liberty)) {
            throw std::runtime_error("Failed to open liberty file: " + liberty);
        }
    }

    void analyse_yosys() {
        analyse_yosys_impl(file_path, top_module, liberty_path, results, metrics);
    }

    void analyse_openroad() {
        analyse_openroad_impl(metrics);
    }

    void evaluate() {
        analyse_yosys();
        analyse_openroad();
        metrics.total_runtime = metrics.yosys_runtime + metrics.openroad_runtime;
    }

    void save(const std::string& output_file) {
        write_string_to_file(output_file, format_metrics_report(metrics));
    }

    const Metrics& get_metrics() const {
        return metrics;
    }
};

#endif // ATLAS_UTILS_H