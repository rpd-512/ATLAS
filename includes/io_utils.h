
#ifndef IO_UTILS_H
#define IO_UTILS_H

#include "types.h"

// Pin names Yosys typically doesn't mark as inputs in port_directions;
// fallback used only if a cell has no "port_directions" entry at all.
static const std::unordered_set<std::string> OUTPUT_PIN_NAMES = {
    "Y", "X", "Q", "QN", "Z", "CO", "COUT", "SO", "S", "SUM"
};


using raw_bit_t = int64_t;

constexpr raw_bit_t RAW_CONST_0 = -1;
constexpr raw_bit_t RAW_CONST_1 = -2;
constexpr raw_bit_t RAW_CONST_X = -3;
constexpr raw_bit_t RAW_CONST_Z = -4;

inline bool raw_is_const(raw_bit_t b) {
    return b < 0;
}

inline raw_bit_t raw_bit_from_json(const nlohmann::ordered_json& b) {
    if (b.is_string()) {
        const std::string& s = b.get_ref<const std::string&>();
        if (s == "0") return RAW_CONST_0;
        if (s == "1") return RAW_CONST_1;
        if (s == "x") return RAW_CONST_X;
        if (s == "z") return RAW_CONST_Z;
        throw std::runtime_error("Unrecognized constant bit string: '" + s + "'");
    }
    return b.get<raw_bit_t>();
}

inline std::vector<raw_bit_t> raw_bits_from_json(const nlohmann::ordered_json& bits_array) {
    std::vector<raw_bit_t> out;
    out.reserve(bits_array.size());
    for (const auto& b : bits_array) {
        out.push_back(raw_bit_from_json(b));
    }
    return out;
}

// Resolve a constant raw_bit_t to a fixed wire_id (WIRE_CONST_0/1), or
// throw for unresolvable x/z -- mirrors evaluate-time behavior in the
// original Python (value_of() raised there for x/z), just moved up to
// parse time since we have nowhere else to park an x/z wire_id.
inline wire_id resolve_const(raw_bit_t b) {
    if (b == RAW_CONST_0) return WIRE_CONST_0;
    if (b == RAW_CONST_1) return WIRE_CONST_1;
    throw std::runtime_error("Unresolvable constant bit (x/z) in netlist");
}

// Drive strength (trailing _0, _1, _2, _4, _8, ...) doesn't change a
// SKY130 cell's boolean function, only its sizing, so it's stripped
// before functional dispatch (e.g. "o21ai_2" and "o21ai_1" both become
// "o21ai").
inline std::string strip_drive_strength(const std::string& cell_type) {
    size_t pos = cell_type.find_last_of('_');
    if (pos == std::string::npos || pos + 1 >= cell_type.size()) return cell_type;
    for (size_t i = pos + 1; i < cell_type.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(cell_type[i]))) return cell_type;
    }
    return cell_type.substr(0, pos);
}

// A cell type like "sky130_fd_sc_hd__nand2_1" is library-prefixed with
// "__"; keep only the part after the last "__".
inline std::string strip_library_prefix(const std::string& cell_type) {
    size_t pos = cell_type.rfind("__");
    if (pos == std::string::npos) return cell_type;
    return cell_type.substr(pos + 2);
}

// ----------------------------------------------------------------------
// parse_netlist -- read one module of a Yosys JSON netlist into a Circuit.
//
// module_name selects which module to parse; if empty, the first module
// found in the file is used (JSON module order is not guaranteed, so
// pass an explicit name for anything but a single-module file).
// ----------------------------------------------------------------------
inline Circuit parse_netlist(const std::string& netlist_path, const std::string& module_name = "") {
    std::ifstream f(netlist_path);
    if (!f) {
        throw std::runtime_error("Could not open netlist file: " + netlist_path);
    }

    nlohmann::ordered_json root;
    f >> root;

    auto modules_it = root.find("modules");
    if (modules_it == root.end() || !modules_it->is_object() || modules_it->empty()) {
        throw std::runtime_error("Netlist has no modules: " + netlist_path);
    }
    const auto& modules = *modules_it;

    nlohmann::ordered_json::const_iterator mod_it;
    if (module_name.empty()) {
        mod_it = modules.begin();
    } else {
        mod_it = modules.find(module_name);
        if (mod_it == modules.end()) {
            throw std::runtime_error("Module '" + module_name + "' not found in netlist: " + netlist_path);
        }
    }
    const auto& module = mod_it.value();

    // ---- ports: collect primary input/output raw bits, in port-bit order ----
    std::vector<raw_bit_t> in_pin, out_pin;
    if (auto ports_it = module.find("ports"); ports_it != module.end()) {
        for (auto p = ports_it->begin(); p != ports_it->end(); ++p) {
            const std::string dir = p.value().at("direction").get<std::string>();
            std::vector<raw_bit_t> bits = raw_bits_from_json(p.value().at("bits"));
            if (dir == "input") {
                in_pin.insert(in_pin.end(), bits.begin(), bits.end());
            } else if (dir == "output") {
                out_pin.insert(out_pin.end(), bits.begin(), bits.end());
            }
        }
    }

    // ---- pass 1: collect every real (non-const) bit + per-cell in/out nets ----
    std::set<raw_bit_t> all_bits;
    for (raw_bit_t b : in_pin)  if (!raw_is_const(b)) all_bits.insert(b);
    for (raw_bit_t b : out_pin) if (!raw_is_const(b)) all_bits.insert(b);

    struct RawCell {
        std::string id;
        std::string raw_type;                 // as written in the netlist
        std::vector<raw_bit_t> in_nets;
        std::vector<raw_bit_t> out_nets;
    };
    std::vector<RawCell> raw_cells;

    if (auto cells_it = module.find("cells"); cells_it != module.end()) {
        for (auto c = cells_it->begin(); c != cells_it->end(); ++c) {
            const auto& cell = c.value();
            RawCell rc;
            rc.id = c.key();
            rc.raw_type = cell.at("type").get<std::string>();

            nlohmann::ordered_json port_dirs;
            if (auto pd_it = cell.find("port_directions"); pd_it != cell.end()) {
                port_dirs = *pd_it;
            }

            const auto& connections = cell.at("connections");
            for (auto conn = connections.begin(); conn != connections.end(); ++conn) {
                const std::string& pin = conn.key();

                std::string direction;
                if (auto d = port_dirs.find(pin); port_dirs.is_object() && d != port_dirs.end()) {
                    direction = d.value().get<std::string>();
                } else {
                    std::string upper = pin;
                    std::transform(upper.begin(), upper.end(), upper.begin(),
                                    [](unsigned char ch) { return std::toupper(ch); });
                    direction = OUTPUT_PIN_NAMES.count(upper) ? "output" : "input";
                }

                std::vector<raw_bit_t> bits = raw_bits_from_json(conn.value());
                for (raw_bit_t b : bits) {
                    if (direction == "output") {
                        rc.out_nets.push_back(b);
                        if (!raw_is_const(b)) all_bits.insert(b);
                    } else if (direction == "input") {
                        rc.in_nets.push_back(b);
                        if (!raw_is_const(b)) all_bits.insert(b);
                    }
                }
            }

            raw_cells.push_back(std::move(rc));
        }
    }

    // ---- pass 2: dense remap for all real signals seen ----
    // sorted() order, same as the original Python, so ids are stable/reproducible
    std::unordered_map<raw_bit_t, wire_id> remap;
    remap.reserve(all_bits.size());
    {
        wire_id next = 0;
        for (raw_bit_t b : all_bits) remap[b] = next++;
    }
    if (remap.size() >= static_cast<size_t>(WIRE_CONST_0)) {
        throw std::runtime_error("parse_netlist: too many wires, ran into reserved constant wire_id range");
    }

    auto r = [&](raw_bit_t b) -> wire_id {
        return raw_is_const(b) ? resolve_const(b) : remap.at(b);
    };

    // ---- pass 3: build Circuit ----
    Circuit circuit;
    circuit.inputs.reserve(in_pin.size());
    for (raw_bit_t b : in_pin) circuit.inputs.push_back(r(b));

    circuit.outputs.reserve(out_pin.size());
    for (raw_bit_t b : out_pin) circuit.outputs.push_back(r(b));

    circuit.gates.reserve(raw_cells.size());
    for (const RawCell& rc : raw_cells) {
        Gate g;
        g.id = rc.id;
        g.data.name = rc.raw_type;
        g.data.type = strip_drive_strength(strip_library_prefix(rc.raw_type));
        g.data.gate_type = gate_type_from_string(g.data.type);   // <-- only new line

        g.outputs.reserve(rc.out_nets.size());
        for (raw_bit_t b : rc.out_nets) g.outputs.push_back(r(b));

        g.inputs.reserve(rc.in_nets.size());
        for (raw_bit_t b : rc.in_nets) g.inputs.push_back(r(b));

        circuit.gates.push_back(std::move(g));
    }

    return circuit;
}

#endif // IO_UTILS_H