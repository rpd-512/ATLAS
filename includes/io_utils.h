
#ifndef IO_UTILS_H
#define IO_UTILS_H

#include "types.h"

// Pin names Yosys typically doesn't mark as inputs in port_directions;
// fallback used only if a cell has no "port_directions" entry at all.
static const std::unordered_set<std::string> OUTPUT_PIN_NAMES = {
    "Y", "X", "Q", "QN", "Z", "CO", "COUT", "SO", "SUM"
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
        g.outputs.reserve(rc.out_nets.size());
        
        for (raw_bit_t b : rc.out_nets) g.outputs.push_back(r(b));

        g.inputs.reserve(rc.in_nets.size());
        for (raw_bit_t b : rc.in_nets) g.inputs.push_back(r(b));

        circuit.gates.push_back(std::move(g));
    }

    return circuit;
}


// Area Eval
struct LibertyLibrary {
    std::unordered_map<std::string, LibertyCellData> cells_library;
    float nom_voltage = 0.0f;
};

inline LibertyLibrary parse_liberty(const std::string& liberty_path) {
    std::ifstream f(liberty_path);
    if (!f) {
        throw std::runtime_error("Could not open liberty file: " + liberty_path);
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // cell ( "name" ) {   or   cell ( name ) {
    static const std::regex cell_pattern(R"RGX(\bcell\s*\(\s*"?([^")]+)"?\s*\)\s*\{)RGX");
    static const std::regex area_pattern(
        R"RGX(\barea\s*:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*;)RGX");
    // cell_leakage_power : X; -- scalar leakage attribute, distinct from the
    // per-state leakage_power() { when: ...; value: ...; } sub-blocks.
    static const std::regex leakage_power_pattern(
        R"RGX(\bcell_leakage_power\s*:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*;)RGX");
    // library-level scalar, e.g. "nom_voltage : 1.8000000000;"
    static const std::regex nom_voltage_pattern(
        R"RGX(\bnom_voltage\s*:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*;)RGX");
    // pin ( "name" ) {   or   pin ( name ) {
    static const std::regex pin_pattern(R"RGX(\bpin\s*\(\s*"?([^")]+)"?\s*\)\s*\{)RGX");
    // \b before "capacitance" excludes rise_capacitance/fall_capacitance.
    static const std::regex capacitance_pattern(
        R"RGX(\bcapacitance\s*:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*;)RGX");
    // function : "..."; -- Liberty's per-output boolean function, e.g.
    // pin(Y) { function : "(A&!B&!C)|(!A&B&!C)"; ... }
    static const std::regex function_pattern(
        R"RGX(\bfunction\s*:\s*"([^"]*)"\s*;)RGX");
    static const std::regex index1_pattern(R"RGX(index_1\s*\(\s*"([^"]*)"\s*\))RGX");
    static const std::regex index2_pattern(R"RGX(index_2\s*\(\s*"([^"]*)"\s*\))RGX");
    static const std::regex num_pattern(R"RGX([+-]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?)RGX");

    // timing ( ) { ... } -- header has no name inside the parens, unlike
    // cell(...)/pin(...)/cell_rise(...)/etc.
    static const std::regex timing_pattern(R"RGX(\btiming\s*\(\s*\)\s*\{)RGX");
    static const std::regex related_pin_pattern(R"RGX(\brelated_pin\s*:\s*"([^"]+)")RGX");
    static const std::regex timing_sense_pattern(R"RGX(\btiming_sense\s*:\s*"([^"]+)")RGX");
    static const std::regex values_kw_pattern(R"RGX(\bvalues\s*\()RGX");

    auto parse_float_list = [](const std::string& s) {
        std::vector<float> out;
        for (auto it = std::sregex_iterator(s.begin(), s.end(), num_pattern); it != std::sregex_iterator(); ++it) {
            out.push_back(std::stof(it->str()));
        }
        return out;
    };

    // Generic "extract up to matching close delimiter" helper, given text
    // starting just past the opening delimiter.
    auto extract_delim = [&](const std::string& text, size_t start, char open, char close) -> std::string {
        int depth = 1;
        size_t i = start;
        while (i < text.size() && depth > 0) {
            if (text[i] == open) ++depth;
            else if (text[i] == close) --depth;
            ++i;
        }
        return text.substr(start, i - start - 1);
    };
    auto extract_block = [&](const std::string& text, size_t start) -> std::string {
        return extract_delim(text, start, '{', '}');
    };

    // Find a named sub-block like cell_rise("del_1_7_7") { ... } inside `text`
    // and return its contents (empty string if absent).
    auto find_named_subblock = [&](const std::string& text, const std::string& name) -> std::string {
        std::regex header_pattern("\\b" + name + R"RGX(\s*\(\s*"?[^")]*"?\s*\)\s*\{)RGX");
        std::smatch m;
        if (!std::regex_search(text, m, header_pattern)) return std::string();
        size_t start = static_cast<size_t>(m.position(0) + m.length(0));
        return extract_block(text, start);
    };

    // One NLDM lookup table: index_1 (input transition) x index_2 (output
    // load) -> values.
    struct NldmTable {
        std::vector<float> index_1;
        std::vector<float> index_2;
        std::vector<std::vector<float>> values;
        bool valid = false;
    };
    auto parse_nldm_table = [&](const std::string& block_text) -> NldmTable {
        NldmTable t;
        if (block_text.empty()) return t;
        std::smatch m;
        if (std::regex_search(block_text, m, index1_pattern)) t.index_1 = parse_float_list(m[1].str());
        if (std::regex_search(block_text, m, index2_pattern)) t.index_2 = parse_float_list(m[1].str());
        if (std::regex_search(block_text, m, values_kw_pattern)) {
            size_t start = static_cast<size_t>(m.position(0) + m.length(0));
            std::string values_text = extract_delim(block_text, start, '(', ')');
            static const std::regex row_pattern(R"RGX("([^"]*)")RGX");
            for (auto it = std::sregex_iterator(values_text.begin(), values_text.end(), row_pattern);
                 it != std::sregex_iterator(); ++it) {
                t.values.push_back(parse_float_list((*it)[1].str()));
            }
        }
        t.valid = !t.index_1.empty() && !t.index_2.empty() && !t.values.empty();
        return t;
    };

    // Element-wise average of rise/fall tables for one unate arc (falls
    // back to whichever side is present if only one is).
    auto average_tables = [](const NldmTable& a, const NldmTable& b) -> std::vector<std::vector<float>> {
        if (!a.valid && !b.valid) return {};
        if (!a.valid) return b.values;
        if (!b.valid) return a.values;
        std::vector<std::vector<float>> out = a.values;
        for (size_t i = 0; i < out.size() && i < b.values.size(); ++i)
            for (size_t j = 0; j < out[i].size() && j < b.values[i].size(); ++j)
                out[i][j] = 0.5f * (a.values[i][j] + b.values[i][j]);
        return out;
    };

    // Element-wise max across positive_unate / negative_unate (falls back
    // to whichever unate is present if only one was found).
    auto max_tables = [](const std::vector<std::vector<float>>& a,
                          const std::vector<std::vector<float>>& b) -> std::vector<std::vector<float>> {
        if (a.empty()) return b;
        if (b.empty()) return a;
        std::vector<std::vector<float>> out = a;
        for (size_t i = 0; i < out.size() && i < b.size(); ++i)
            for (size_t j = 0; j < out[i].size() && j < b[i].size(); ++j)
                out[i][j] = std::max(a[i][j], b[i][j]);
        return out;
    };

    // Averaged (rise+fall)/2 delay & slew tables for a single unate arc,
    // keyed later by related_pin + timing_sense.
    struct UnateArc {
        std::vector<float> index_1, index_2;
        std::vector<std::vector<float>> delay;  // avg(cell_rise, cell_fall)
        std::vector<std::vector<float>> slew;   // avg(rise_transition, fall_transition)
        bool has_index = false;
    };

    LibertyLibrary cells;

    // Library-level nom_voltage: appears once, before any cell(...) blocks,
    // so the first match anywhere in the file is the library-level value.
    {
        std::smatch nv_match;
        if (std::regex_search(data, nv_match, nom_voltage_pattern)) {
            cells.nom_voltage = std::stof(nv_match[1].str());
        }
    }

    auto cell_begin = std::sregex_iterator(data.begin(), data.end(), cell_pattern);
    auto cell_end   = std::sregex_iterator();

    for (auto it = cell_begin; it != cell_end; ++it) {
        const std::smatch& match = *it;
        std::string cell_name = match[1].str();

        size_t cell_start = static_cast<size_t>(match.position(0) + match.length(0));
        std::string cell_block = extract_block(data, cell_start);

        std::smatch area_match;
        if (!std::regex_search(cell_block, area_match, area_pattern)) continue;

        LibertyCellData entry;
        entry.name = cell_name;
        entry.area = std::stod(area_match[1].str());

        std::smatch leakage_match;
        if (std::regex_search(cell_block, leakage_match, leakage_power_pattern)) {
            entry.leakage_power = std::stof(leakage_match[1].str());
        }

        // ---- pass A: collect every timing() sub-block anywhere in the cell,
        // grouped by (related_pin, timing_sense). These live inside the
        // *output* pin's block, not the input pin's own block, so this scans
        // the whole cell rather than per-pin. ----
        std::unordered_map<std::string, std::unordered_map<std::string, UnateArc>> pin_timing;

        for (auto tit = std::sregex_iterator(cell_block.begin(), cell_block.end(), timing_pattern);
             tit != std::sregex_iterator(); ++tit) {
            size_t t_start = static_cast<size_t>(tit->position(0) + tit->length(0));
            std::string timing_block = extract_block(cell_block, t_start);

            std::smatch m;
            if (!std::regex_search(timing_block, m, related_pin_pattern)) continue;
            std::string related_pin = m[1].str();
            std::string timing_sense = "positive_unate";
            if (std::regex_search(timing_block, m, timing_sense_pattern)) timing_sense = m[1].str();

            NldmTable cell_rise = parse_nldm_table(find_named_subblock(timing_block, "cell_rise"));
            NldmTable cell_fall = parse_nldm_table(find_named_subblock(timing_block, "cell_fall"));
            NldmTable rise_tr   = parse_nldm_table(find_named_subblock(timing_block, "rise_transition"));
            NldmTable fall_tr   = parse_nldm_table(find_named_subblock(timing_block, "fall_transition"));

            UnateArc arc;
            const NldmTable& idx_source = cell_rise.valid ? cell_rise : cell_fall;
            if (idx_source.valid) {
                arc.index_1 = idx_source.index_1;
                arc.index_2 = idx_source.index_2;
                arc.has_index = true;
            }
            arc.delay = average_tables(cell_rise, cell_fall);
            arc.slew  = average_tables(rise_tr, fall_tr);

            pin_timing[related_pin][timing_sense] = std::move(arc);
        }

        // ---- pass B: walk each pin(...) sub-block in file order. Input
        // pins (have `capacitance`) get a dense pin_index and their timing
        // arc pulled from pin_timing; output pins (have `function` instead)
        // get their boolean function captured, in file order. ----
        auto pin_begin = std::sregex_iterator(cell_block.begin(), cell_block.end(), pin_pattern);
        auto pin_end   = std::sregex_iterator();

        size_t pin_index = 0;
        for (auto pit = pin_begin; pit != pin_end && pin_index < entry.input_capacitances.size(); ++pit) {
            const std::smatch& pmatch = *pit;
            std::string pin_name = pmatch[1].str();
            size_t pin_start = static_cast<size_t>(pmatch.position(0) + pmatch.length(0));
            std::string pin_block = extract_block(cell_block, pin_start);

            // capacitance: only meaningful on input pins; output pins don't
            // carry one, so a missing match here means it's (probably) an
            // output pin instead -- check for `function` there and skip
            // without consuming a pin_index slot either way.
            std::smatch cap_match;
            if (!std::regex_search(pin_block, cap_match, capacitance_pattern)) {
                std::smatch fn_match;
                if (std::regex_search(pin_block, fn_match, function_pattern)) {
                    entry.output_names.push_back(pin_name);
                    entry.function_strings.push_back(fn_match[1].str());
                }
                continue;
            }
            entry.input_capacitances[pin_index] = std::stof(cap_match[1].str());
            entry.pin_index[pin_name] = static_cast<int>(pin_index);

            PinTimingArc& arc = entry.pin_arcs[pin_index];

            auto pt_it = pin_timing.find(pin_name);
            if (pt_it != pin_timing.end()) {
                std::vector<std::vector<float>> delay_max, slew_max;
                std::vector<float> index_1, index_2;
                bool have_index = false;

                // max across positive_unate / negative_unate (each side
                // already averaged rise+fall in pass A)
                for (const auto& [sense, arc_data] : pt_it->second) {
                    (void)sense;
                    delay_max = max_tables(delay_max, arc_data.delay);
                    slew_max  = max_tables(slew_max, arc_data.slew);
                    if (!have_index && arc_data.has_index) {
                        index_1 = arc_data.index_1;
                        index_2 = arc_data.index_2;
                        have_index = true;
                    }
                }

                if (have_index && !index_1.empty() && !index_2.empty()) {
                    arc.min_transition = *std::min_element(index_1.begin(), index_1.end());
                    arc.max_transition = *std::max_element(index_1.begin(), index_1.end());
                    arc.min_load = *std::min_element(index_2.begin(), index_2.end());
                    arc.max_load = *std::max_element(index_2.begin(), index_2.end());

                    if (!delay_max.empty())
                        arc.propagation_coeffs = fit_nldm_coeffs(index_1, index_2, delay_max);
                    if (!slew_max.empty())
                        arc.slew_coeffs = fit_nldm_coeffs(index_1, index_2, slew_max);
                }
            }

            ++pin_index;
        }

        entry.num_pins = static_cast<int>(pin_index);

        cells.cells_library[cell_name] = std::move(entry);
    }

    return cells;
}

#endif // IO_UTILS_H
