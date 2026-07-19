#ifndef OPENROAD_UTILS_H
#define OPENROAD_UTILS_H

#include <chrono>
#include <sstream>
#include <regex>
#include <cstdio>
#include <iostream>

#include "atlas_utils.h"
#include "io_utils.h"

inline std::string build_openroad_script(const std::string& netlist_path,
                                          const std::string& liberty_path,
                                          const std::string& lef_path,
                                          const std::string& top_module,
                                          const std::string& clk_port,
                                          double clk_period_ns) {
    std::ostringstream ss;
    ss << "read_lef " << lef_path << "\n"
       << "read_liberty " << liberty_path << "\n"
       << "read_verilog " << netlist_path << "\n"
       << "link_design " << top_module << "\n";

    if (!clk_port.empty()) {
        // Clocked design: real clock on the named port.
        ss << "create_clock -name clk -period " << clk_period_ns
           << " [get_ports " << clk_port << "]\n";
    } else {
        // Combinational design: no clock port exists. A virtual clock
        // gives report_checks a timing reference so it can trace
        // input->output paths, with zero I/O delay so slack == the
        // raw combinational delay.
        ss << "create_clock -name virtual_clk -period " << clk_period_ns << "\n"
           << "set_input_delay 0 -clock virtual_clk [all_inputs]\n"
           << "set_output_delay 0 -clock virtual_clk [all_outputs]\n";
    }

    ss << "report_checks -path_delay max -format full_clock_expanded\n"
       << "report_power\n"
       << "exit\n";
    return ss.str();
}

inline void parse_timing(const std::string& log, Metrics& metrics) {
    std::smatch m;

    static const std::regex arrival_re(
        R"(([\-0-9.]+)\s+data arrival time)");
    if (std::regex_search(log, m, arrival_re)) {
        metrics.critical_path = std::stod(m[1]);
    }

    static const std::regex slack_re(
        R"(([\-0-9.]+)\s+slack\s+\((MET|VIOLATED)\))");
    if (std::regex_search(log, m, slack_re)) {
        metrics.worst_slack = std::stod(m[1]);
    }
}

inline void parse_power(const std::string& log, Metrics& metrics) {
    static const std::regex power_re(
        R"(Total\s+([\-0-9.e+]+)\s+([\-0-9.e+]+)\s+([\-0-9.e+]+)\s+([\-0-9.e+]+))");
    std::smatch m;
    if (std::regex_search(log, m, power_re)) {
        metrics.internal_power  = std::stod(m[1]);
        metrics.switching_power = std::stod(m[2]);
        metrics.leakage_power   = std::stod(m[3]);
        metrics.total_power     = std::stod(m[4]);
    }
}

inline void analyse_openroad_impl(Metrics& metrics,
                                   const std::string& netlist_path,
                                   const std::string& liberty_path,
                                   const std::string& lef_path,             // NEW
                                   const std::string& top_module,
                                   const std::string& clk_port,
                                   double clk_period_ns) {
    char script_tmpl[] = "/tmp/atlas_or_XXXXXX.tcl";
    int script_fd = mkstemps(script_tmpl, 4);   // ".tcl" = 4 chars
    if (script_fd == -1) {
        throw std::runtime_error("Failed to create temp OpenROAD script");
    }
    close(script_fd);

    char log_tmpl[] = "/tmp/atlas_or_log_XXXXXX.txt";
    int log_fd = mkstemps(log_tmpl, 4);   // ".txt" = 4 chars
    if (log_fd == -1) {
        throw std::runtime_error("Failed to create temp OpenROAD log");
    }
    close(log_fd);

    std::string script = build_openroad_script(
        netlist_path, liberty_path, lef_path, top_module, clk_port, clk_period_ns);
    write_string_to_file(script_tmpl, script);

    std::string command =
        "openroad -exit " + std::string(script_tmpl) +
        " > " + std::string(log_tmpl) + " 2>&1";

    auto start_time = std::chrono::steady_clock::now();
    int result = std::system(command.c_str());
    auto end_time = std::chrono::steady_clock::now();
    metrics.openroad_runtime =
        std::chrono::duration<double>(end_time - start_time).count();

    std::string log = read_file_to_string(log_tmpl);
    std::remove(script_tmpl);
    std::remove(log_tmpl);
    std::remove(netlist_path.c_str());

    if (result != 0) {
        std::cerr << "OpenROAD failed:\n" << log << "\n";
        return;
    }

    parse_timing(log, metrics);
    parse_power(log, metrics);
}

#endif // OPENROAD_UTILS_H