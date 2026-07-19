#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <regex>
#include <iomanip>
#include <nlohmann/json.hpp>


// Isolates the JSON object from yosys's mixed stdout+stderr log.
// Finds "creator" (a key only the stat -json block has) and walks
// backward/forward via brace-depth counting to isolate the matching {...}.
inline std::string extract_json_block(const std::string& text) {
    size_t marker = text.find("\"creator\"");
    if (marker == std::string::npos) {
        throw std::runtime_error("No JSON block found in yosys output");
    }

    size_t start = text.rfind('{', marker);
    if (start == std::string::npos) {
        throw std::runtime_error("Malformed JSON block (no opening brace)");
    }

    int depth = 0;
    size_t end = std::string::npos;
    for (size_t i = start; i < text.size(); ++i) {
        if (text[i] == '{') depth++;
        else if (text[i] == '}') {
            depth--;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    if (end == std::string::npos) {
        throw std::runtime_error("Malformed JSON block (unbalanced braces)");
    }

    return text.substr(start, end - start + 1);
}

// Reads an entire file into a string. Throws if the file can't be opened.
inline std::string read_file_to_string(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open: " + path);
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// Checks a file exists/opens without reading its contents. Used for
// early validation (e.g. in Atlas's constructor).
inline bool file_is_readable(const std::string& path) {
    std::ifstream input(path);
    return input.is_open();
}

// Writes a string to a file, overwriting any existing content.
inline void write_string_to_file(const std::string& path, const std::string& content) {
    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open: " + path);
    }
    output << content;
}


inline std::string format_metrics_report(const Metrics& m) {
    nlohmann::json j;

    j["design"] = {
        {"name",          m.design_name},
        {"source_file",   m.source_file},
        {"liberty_file",  m.liberty_file},
        {"technology",    m.technology},
        {"pvt_corner",    m.pvt_corner},
        {"yosys_version", m.yosys_version}
    };

    j["area"] = {
        {"unit",          "um^2"},
        {"total",         m.area},
        {"combinational", m.combinational_area},
        {"sequential",    m.sequential_area}
    };

    j["timing"] = {
        {"unit",          "ns"},
        {"critical_path", m.critical_path ? nlohmann::json(*m.critical_path) : nlohmann::json(nullptr)},
        {"worst_slack",   m.worst_slack   ? nlohmann::json(*m.worst_slack)   : nlohmann::json(nullptr)}
    };

    j["power"] = {
        {"unit",             "W"},
        {"internal_power",  m.internal_power  ? nlohmann::json(*m.internal_power)  : nlohmann::json(nullptr)},
        {"switching_power", m.switching_power ? nlohmann::json(*m.switching_power) : nlohmann::json(nullptr)},
        {"leakage_power",   m.leakage_power   ? nlohmann::json(*m.leakage_power)   : nlohmann::json(nullptr)},
        {"total_power",     m.total_power     ? nlohmann::json(*m.total_power)     : nlohmann::json(nullptr)}
    };

    j["cells"] = {
        {"total",         m.total_cells},
        {"combinational", m.combinational_cells},
        {"sequential",    m.sequential_cells},
        {"by_type",       m.cell_count}
    };

    j["structure"] = {
        {"num_wires",         m.num_wires},
        {"num_wire_bits",     m.num_wire_bits},
        {"num_pub_wires",     m.num_pub_wires},
        {"num_pub_wire_bits", m.num_pub_wire_bits},
        {"num_ports",         m.num_ports},
        {"num_port_bits",     m.num_port_bits},
        {"num_memories",      m.num_memories},
        {"num_memory_bits",   m.num_memory_bits},
        {"num_submodules",    m.num_submodules}
    };

    j["runtime"] = {
        {"unit",     "s"},
        {"yosys",    m.yosys_runtime},
        {"openroad", m.openroad_runtime},
        {"total",    m.total_runtime}
    };

    return j.dump(4); // pretty-printed, 4-space indent
}

#endif // IO_UTILS_H