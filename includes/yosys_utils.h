#ifndef YOSYS_UTILS_H
#define YOSYS_UTILS_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <chrono>

#include <nlohmann/json.hpp>

#include "atlas_utils.h"  // for Metrics
#include "io_utils.h"     // for extract_json_block

// Heuristic classifier: sky130 (and most PDKs) name sequential cells
// with recognizable substrings. Not perfect across all libraries, but
// works for common ones.
inline bool is_sequential_cell(const std::string& cell_name) {
    static const std::vector<std::string> seq_markers = {
        "dfxtp", "dfrtp", "dfrtn", "dfstp", "edfxtp",
        "dfbbn", "dfbbp", "sdfxtp", "sdfrtp", "dlxtp", "dlxbp"
    };
    for (const auto& marker : seq_markers) {
        if (cell_name.find(marker) != std::string::npos) return true;
    }
    return false;
}

// Runs yosys synthesis (mapped to the given liberty library) on
// file_path, capturing stat -json output into `results` and parsing
// it into `metrics`.
inline void analyse_yosys_impl(const std::string& file_path,
                                const std::string& top_module,
                                const std::string& liberty_path,
                                std::string& results,
                                Metrics& metrics,
                                std::string& netlist_path) {   // out-param again
    char out_tmpl[] = "/tmp/atlas_stat_XXXXXX.json";
    int out_fd = mkstemps(out_tmpl, 5);
    if (out_fd == -1) {
        throw std::runtime_error("Failed to create temp stat file");
    }
    close(out_fd);

    char netlist_tmpl[] = "/tmp/atlas_netlist_XXXXXX.v";
    int netlist_fd = mkstemps(netlist_tmpl, 2);   // ".v" = 2 chars
    if (netlist_fd == -1) {
        throw std::runtime_error("Failed to create temp netlist file");
    }
    close(netlist_fd);
    netlist_path = netlist_tmpl;

    std::string script =
        "read_verilog -sv " + file_path + "; "
        "synth -top " + top_module + " -noabc; "
        "dfflibmap -liberty " + liberty_path + "; "
        "abc -liberty " + liberty_path + "; "
        "opt_clean; "
        "write_verilog -noattr " + netlist_path + "; "
        "stat -liberty " + liberty_path + " -json";

    std::string command =
        "yosys -p \"" + script + "\" > " + std::string(out_tmpl) + " 2>&1";

    auto start_time = std::chrono::steady_clock::now();
    int result = std::system(command.c_str());
    auto end_time = std::chrono::steady_clock::now();

    metrics.yosys_runtime =
        std::chrono::duration<double>(end_time - start_time).count();

    std::ifstream stat_in(out_tmpl);
    if (stat_in.is_open()) {
        std::stringstream buffer;
        buffer << stat_in.rdbuf();
        results += buffer.str();
    }
    std::remove(out_tmpl);

    if (result != 0) {
        std::cerr << "Yosys failed\n";
        return; // nothing to parse if yosys itself failed
    }

    // --- Parse the JSON block and populate metrics ---
    std::string json_text = extract_json_block(results);
    nlohmann::json parsed = nlohmann::json::parse(json_text);

    metrics.design_name   = top_module;
    metrics.source_file   = file_path;
    metrics.liberty_file  = liberty_path;
    metrics.synth_command = script;
    metrics.yosys_version = parsed.value("creator", "");

    const auto& design = parsed["design"];

    metrics.area               = design.value("area", 0.0);
    metrics.sequential_area    = design.value("sequential_area", 0.0);
    metrics.combinational_area = metrics.area - metrics.sequential_area;

    metrics.total_cells       = design.value("num_cells", 0);
    metrics.num_wires         = design.value("num_wires", 0);
    metrics.num_wire_bits     = design.value("num_wire_bits", 0);
    metrics.num_pub_wires     = design.value("num_pub_wires", 0);
    metrics.num_pub_wire_bits = design.value("num_pub_wire_bits", 0);
    metrics.num_ports         = design.value("num_ports", 0);
    metrics.num_port_bits     = design.value("num_port_bits", 0);
    metrics.num_memories      = design.value("num_memories", 0);
    metrics.num_memory_bits   = design.value("num_memory_bits", 0);
    metrics.num_submodules    = design.value("num_submodules", 0);

    metrics.cell_count.clear();
    metrics.sequential_cells = 0;
    metrics.combinational_cells = 0;

    if (design.contains("num_cells_by_type")) {
        for (auto& [cell_name, count] : design["num_cells_by_type"].items()) {
            int c = count.get<int>();
            metrics.cell_count[cell_name] = c;

            if (is_sequential_cell(cell_name)) {
                metrics.sequential_cells += c;
            } else {
                metrics.combinational_cells += c;
            }
        }
    }
}

#endif // YOSYS_UTILS_H