#ifndef TYPES_H
#define TYPES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>

using wire_id = uint32_t;
using SignalArray = std::vector<bool>;

constexpr wire_id WIRE_CONST_0 = std::numeric_limits<wire_id>::max() - 1;
constexpr wire_id WIRE_CONST_1 = std::numeric_limits<wire_id>::max();

// ----------------------------------------------------------------------
// Gate type enum + library. Replaces the old string-keyed gate_fns
// namespace: dispatch is now a switch on an enum resolved once at parse
// time (see io_utils.h), instead of an unordered_map<string, GateFn>
// lookup on every single evaluate() call.
// ----------------------------------------------------------------------
enum class GateType
{
    BUF, INV, CLKINV,
    AND2, OR2, NAND2, NOR2, XOR2, XNOR2,
    NAND3, NOR3, AND3, OR3,
    OR4, AND4, NAND4, NOR4,
    MUX2, NAND2B, NOR2B,
    A21OI, A211O, A22OI, A221O, O21AI, O211A, OAI21,
    A31OI, O31AI,
    HA, FA, MAJ3,
    LPFLOW_ISOBUFSRC
};

constexpr int MAX_ARITY = 5; // a221o

struct GateInfo
{
    GateType type;
    std::string name;      // matches strip_drive_strength/strip_library_prefix output
    std::uint8_t inputs;
    std::uint8_t outputs;  // 1 for everything except ha/fa
};

inline const std::unordered_map<GateType, GateInfo>& GateLibrary()
{
    static const std::unordered_map<GateType, GateInfo> lib = {
        {GateType::BUF,    {GateType::BUF,    "buf",    1, 1}},
        {GateType::INV,    {GateType::INV,    "inv",    1, 1}},
        {GateType::CLKINV, {GateType::CLKINV, "clkinv", 1, 1}},

        {GateType::AND2,  {GateType::AND2,  "and2",  2, 1}},
        {GateType::OR2,   {GateType::OR2,   "or2",   2, 1}},
        {GateType::NAND2, {GateType::NAND2, "nand2", 2, 1}},
        {GateType::NOR2,  {GateType::NOR2,  "nor2",  2, 1}},
        {GateType::XOR2,  {GateType::XOR2,  "xor2",  2, 1}},
        {GateType::XNOR2, {GateType::XNOR2, "xnor2", 2, 1}},

        {GateType::NAND3, {GateType::NAND3, "nand3", 3, 1}},
        {GateType::NOR3,  {GateType::NOR3,  "nor3",  3, 1}},
        {GateType::AND3,  {GateType::AND3,  "and3",  3, 1}},
        {GateType::OR3,   {GateType::OR3,   "or3",   3, 1}},

        {GateType::OR4,   {GateType::OR4,   "or4",   4, 1}},
        {GateType::AND4,  {GateType::AND4,  "and4",  4, 1}},
        {GateType::NAND4, {GateType::NAND4, "nand4", 4, 1}},
        {GateType::NOR4,  {GateType::NOR4,  "nor4",  4, 1}},

        {GateType::MUX2,   {GateType::MUX2,   "mux2",   3, 1}},
        {GateType::NAND2B, {GateType::NAND2B, "nand2b", 2, 1}},
        {GateType::NOR2B,  {GateType::NOR2B,  "nor2b",  2, 1}},

        {GateType::A21OI, {GateType::A21OI, "a21oi", 3, 1}},
        {GateType::A211O, {GateType::A211O, "a211o", 4, 1}},
        {GateType::A22OI, {GateType::A22OI, "a22oi", 4, 1}},
        {GateType::A221O, {GateType::A221O, "a221o", 5, 1}},
        {GateType::O21AI, {GateType::O21AI, "o21ai", 3, 1}},
        {GateType::O211A, {GateType::O211A, "o211a", 4, 1}},
        {GateType::OAI21, {GateType::OAI21, "oai21", 3, 1}},
        {GateType::A31OI, {GateType::A31OI, "a31oi", 4, 1}},
        {GateType::O31AI, {GateType::O31AI, "o31ai", 4, 1}},

        {GateType::HA,   {GateType::HA,   "ha",   2, 2}},
        {GateType::FA,   {GateType::FA,   "fa",   3, 2}},
        {GateType::MAJ3, {GateType::MAJ3, "maj3", 3, 1}},

        {GateType::LPFLOW_ISOBUFSRC, {GateType::LPFLOW_ISOBUFSRC, "lpflow_isobufsrc", 2, 1}},
    };
    return lib;
}

inline const GateInfo& GetGateInfo(GateType type) { return GateLibrary().at(type); }

// Maps a stripped cell-type string (post strip_library_prefix/
// strip_drive_strength) to its GateType, resolved once at parse time.
inline GateType gate_type_from_string(const std::string& s)
{
    static const std::unordered_map<std::string, GateType> rev = [] {
        std::unordered_map<std::string, GateType> m;
        for (const auto& [t, info] : GateLibrary()) m[info.name] = t;
        return m;
    }();
    auto it = rev.find(s);
    if (it == rev.end())
        throw std::runtime_error("gate_type_from_string: unknown cell type '" + s + "'");
    return it->second;
}

// Enum-dispatched evaluator. a[] populated up to GetGateInfo(type).inputs;
// unused trailing slots ignored. Returns up to 2 outputs (ha/fa); callers
// slice to GetGateInfo(type).outputs of them.
inline std::array<bool, 2> evaluate_gate(GateType type, const std::array<bool, MAX_ARITY>& a)
{
    switch (type)
    {
        case GateType::BUF:    return {a[0], false};
        case GateType::INV:    return {!a[0], false};
        case GateType::CLKINV: return {!a[0], false};

        case GateType::AND2:  return {a[0] && a[1], false};
        case GateType::OR2:   return {a[0] || a[1], false};
        case GateType::NAND2: return {!(a[0] && a[1]), false};
        case GateType::NOR2:  return {!(a[0] || a[1]), false};
        case GateType::XOR2:  return {a[0] != a[1], false};
        case GateType::XNOR2: return {a[0] == a[1], false};

        case GateType::NAND3: return {!(a[0] && a[1] && a[2]), false};
        case GateType::NOR3:  return {!(a[0] || a[1] || a[2]), false};
        case GateType::AND3:  return {a[0] && a[1] && a[2], false};
        case GateType::OR3:   return {a[0] || a[1] || a[2], false};

        case GateType::OR4:   return {a[0] || a[1] || a[2] || a[3], false};
        case GateType::AND4:  return {a[0] && a[1] && a[2] && a[3], false};
        case GateType::NAND4: return {!(a[0] && a[1] && a[2] && a[3]), false};
        case GateType::NOR4:  return {!(a[0] || a[1] || a[2] || a[3]), false};

        case GateType::MUX2:   return {a[2] ? a[1] : a[0], false};
        case GateType::NAND2B: return {a[0] || !a[1], false};
        case GateType::NOR2B:  return {!a[0] && !a[1], false};

        case GateType::A21OI: return {!((a[0] && a[1]) || a[2]), false};
        case GateType::A211O: return {(a[0] && a[1]) || a[2] || a[3], false};
        case GateType::A22OI: return {!((a[0] && a[1]) || (a[2] && a[3])), false};
        case GateType::A221O: return {(a[0] && a[1]) || (a[2] && a[3]) || a[4], false};
        case GateType::O21AI: return {!((a[0] || a[1]) && a[2]), false};
        case GateType::O211A: return {((a[0] || a[1]) && a[2]) || a[3], false};
        case GateType::OAI21: return {!((a[0] || a[1]) && a[2]), false};
        case GateType::A31OI: return {!((a[0] && a[1] && a[2]) || a[3]), false};
        case GateType::O31AI: return {!((a[0] || a[1] || a[2]) && a[3]), false};

        case GateType::HA: return {a[0] != a[1], a[0] && a[1]};
        case GateType::FA: {
            bool sum   = (a[0] != a[1]) != a[2];
            bool carry = (a[0] && a[1]) || (a[2] && (a[0] != a[1]));
            return {sum, carry};
        }
        case GateType::MAJ3: return {(a[0] && a[1]) || (a[1] && a[2]) || (a[0] && a[2]), false};

        case GateType::LPFLOW_ISOBUFSRC: return {a[0], false};

        default:
            throw std::runtime_error("evaluate_gate: unhandled GateType");
    }
}

// ----------------------------------------------------------------------
// Phenotype circuit representation (built by io_utils.h's parse_netlist,
// consumed by atlas_utils.h's evaluate_circuit).
// ----------------------------------------------------------------------
struct GateData {
    std::string name;      // raw cell type, e.g. "sky130_fd_sc_hd__nand2_1"
    std::string type;      // stripped, e.g. "nand2"
    GateType gate_type;    // resolved once at parse time, not re-hashed per eval

    std::vector<bool> evaluate(const std::vector<bool>& in) const {
        std::array<bool, MAX_ARITY> a{};
        for (size_t i = 0; i < in.size() && i < a.size(); ++i) a[i] = in[i];
        auto out = evaluate_gate(gate_type, a);
        int n = GetGateInfo(gate_type).outputs;
        return std::vector<bool>(out.begin(), out.begin() + n);
    }
    float area = -1;              // area in um^2, for area-based fitness
};
struct Gate {
    std::string id;                 // cell instance name from the netlist
    std::vector<wire_id> outputs;   // this gate's output wires, in pin order
    GateData data;
    std::vector<wire_id> inputs;    // this gate's input wires, in pin order (may include WIRE_CONST_0/1)
};

struct Circuit {
    std::vector<Gate> gates;
    std::vector<wire_id> inputs;    // primary input wires, in port-bit order
    std::vector<wire_id> outputs;   // primary output wires, in port-bit order
};


struct LibertyCellData {
    std::string name;      // raw liberty cell name, unstripped
    double area = 0.0;
};

using LibertyLibrary = std::unordered_map<std::string, LibertyCellData>;

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

    LibertyLibrary cells;

    auto begin = std::sregex_iterator(data.begin(), data.end(), cell_pattern);
    auto end   = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const std::smatch& match = *it;
        std::string cell_name = match[1].str();

        // Find the matching closing brace for this cell block, same
        // depth-counting approach as the Python version.
        size_t start = static_cast<size_t>(match.position(0) + match.length(0));
        int depth = 1;
        size_t i = start;
        while (i < data.size() && depth > 0) {
            if (data[i] == '{') ++depth;
            else if (data[i] == '}') --depth;
            ++i;
        }
        std::string cell_block = data.substr(start, i - start - 1);

        std::smatch area_match;
        if (std::regex_search(cell_block, area_match, area_pattern)) {
            LibertyCellData entry;
            entry.name = cell_name;
            entry.area = std::stod(area_match[1].str());
            cells[cell_name] = std::move(entry);
        }
    }

    return cells;
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


#endif // TYPES_H