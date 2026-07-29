#ifndef TYPES_H
#define TYPES_H

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using GateFn = std::vector<bool> (*)(const std::vector<bool>&);

namespace gate_fns {

inline void need(const std::vector<bool>& in, size_t n, const char* who) {
    if (in.size() != n) {
        throw std::runtime_error(std::string("Gate '") + who + "' expects " + std::to_string(n) +
                                  " input(s), got " + std::to_string(in.size()));
    }
}

inline std::vector<bool> buf_(const std::vector<bool>& a)    { need(a, 1, "buf");    return {a[0]}; }
inline std::vector<bool> inv_(const std::vector<bool>& a)    { need(a, 1, "inv");    return {!a[0]}; }
inline std::vector<bool> clkinv_(const std::vector<bool>& a) { need(a, 1, "clkinv"); return {!a[0]}; }

inline std::vector<bool> and2_(const std::vector<bool>& a)  { need(a, 2, "and2");  return {a[0] && a[1]}; }
inline std::vector<bool> or2_(const std::vector<bool>& a)   { need(a, 2, "or2");   return {a[0] || a[1]}; }
inline std::vector<bool> nand2_(const std::vector<bool>& a) { need(a, 2, "nand2"); return {!(a[0] && a[1])}; }
inline std::vector<bool> nor2_(const std::vector<bool>& a)  { need(a, 2, "nor2");  return {!(a[0] || a[1])}; }
inline std::vector<bool> xor2_(const std::vector<bool>& a)  { need(a, 2, "xor2");  return {a[0] != a[1]}; }
inline std::vector<bool> xnor2_(const std::vector<bool>& a) { need(a, 2, "xnor2"); return {a[0] == a[1]}; }

inline std::vector<bool> nand3_(const std::vector<bool>& a) { need(a, 3, "nand3"); return {!(a[0] && a[1] && a[2])}; }
inline std::vector<bool> nor3_(const std::vector<bool>& a)  { need(a, 3, "nor3");  return {!(a[0] || a[1] || a[2])}; }
inline std::vector<bool> and3_(const std::vector<bool>& a)  { need(a, 3, "and3");  return {a[0] && a[1] && a[2]}; }
inline std::vector<bool> or3_(const std::vector<bool>& a)   { need(a, 3, "or3");   return {a[0] || a[1] || a[2]}; }

inline std::vector<bool> or4_(const std::vector<bool>& a)   { need(a, 4, "or4");   return {a[0] || a[1] || a[2] || a[3]}; }
inline std::vector<bool> and4_(const std::vector<bool>& a)  { need(a, 4, "and4");  return {a[0] && a[1] && a[2] && a[3]}; }
inline std::vector<bool> nand4_(const std::vector<bool>& a) { need(a, 4, "nand4"); return {!(a[0] && a[1] && a[2] && a[3])}; }
inline std::vector<bool> nor4_(const std::vector<bool>& a)  { need(a, 4, "nor4");  return {!(a[0] || a[1] || a[2] || a[3])}; }

inline std::vector<bool> mux2_(const std::vector<bool>& a)   { need(a, 3, "mux2");   return {a[2] ? a[1] : a[0]}; }   // (a, b, s) -> s ? b : a
inline std::vector<bool> nand2b_(const std::vector<bool>& a) { need(a, 2, "nand2b"); return {a[0] || !a[1]}; }        // (a_n, b)
inline std::vector<bool> nor2b_(const std::vector<bool>& a)  { need(a, 2, "nor2b");  return {!a[0] && !a[1]}; }       // (a_n, b)

inline std::vector<bool> a21oi_(const std::vector<bool>& a) { need(a, 3, "a21oi"); return {!((a[0] && a[1]) || a[2])}; }                // (a1,a2,b1)
inline std::vector<bool> a211o_(const std::vector<bool>& a) { need(a, 4, "a211o"); return {(a[0] && a[1]) || a[2] || a[3]}; }           // (a1,a2,b1,c1)
inline std::vector<bool> a22oi_(const std::vector<bool>& a) { need(a, 4, "a22oi"); return {!((a[0] && a[1]) || (a[2] && a[3]))}; }      // (a1,a2,b1,b2)
inline std::vector<bool> a221o_(const std::vector<bool>& a) { need(a, 5, "a221o"); return {(a[0] && a[1]) || (a[2] && a[3]) || a[4]}; } // (a1,a2,b1,b2,c1)
inline std::vector<bool> o21ai_(const std::vector<bool>& a) { need(a, 3, "o21ai"); return {!((a[0] || a[1]) && a[2])}; }                // (a1,a2,b1)
inline std::vector<bool> o211a_(const std::vector<bool>& a) { need(a, 4, "o211a"); return {((a[0] || a[1]) && a[2]) || a[3]}; }         // (a1,a2,b1,c1)
inline std::vector<bool> oai21_(const std::vector<bool>& a) { need(a, 3, "oai21"); return {!((a[0] || a[1]) && a[2])}; }                // (a1,a2,b1)

inline std::vector<bool> ha_(const std::vector<bool>& a) {   // (sum, carry)
    need(a, 2, "ha");
    return {a[0] != a[1], a[0] && a[1]};
}
inline std::vector<bool> fa_(const std::vector<bool>& a) {   // (sum, carry)
    need(a, 3, "fa");
    bool sum = (a[0] != a[1]) != a[2];
    bool carry = (a[0] && a[1]) || (a[2] && (a[0] != a[1]));
    return {sum, carry};
}
inline std::vector<bool> maj3_(const std::vector<bool>& a) { need(a, 3, "maj3"); return {(a[0] && a[1]) || (a[1] && a[2]) || (a[0] && a[2])}; }

// Power-gating isolation buffer: passthrough of A. SLEEP is a control pin,
// not part of the boolean logic, and is simply not consumed here (present
// only so the arg-count check passes).
inline std::vector<bool> lpflow_isobufsrc_(const std::vector<bool>& a) { need(a, 2, "lpflow_isobufsrc"); return {a[0]}; }

inline const std::unordered_map<std::string, GateFn>& table() {
    static const std::unordered_map<std::string, GateFn> t = {
        {"buf", buf_}, {"inv", inv_}, {"clkinv", clkinv_},
        {"and2", and2_}, {"or2", or2_}, {"nand2", nand2_}, {"nor2", nor2_}, {"xor2", xor2_}, {"xnor2", xnor2_},
        {"nand3", nand3_}, {"nor3", nor3_}, {"and3", and3_}, {"or3", or3_},
        {"or4", or4_}, {"and4", and4_}, {"nand4", nand4_}, {"nor4", nor4_},
        {"mux2", mux2_}, {"nand2b", nand2b_}, {"nor2b", nor2b_},
        {"a21oi", a21oi_}, {"a211o", a211o_}, {"a22oi", a22oi_}, {"a221o", a221o_},
        {"o21ai", o21ai_}, {"o211a", o211a_}, {"oai21", oai21_},
        {"ha", ha_}, {"fa", fa_}, {"maj3", maj3_},
        {"lpflow_isobufsrc", lpflow_isobufsrc_},
    };
    return t;
}

} // namespace gate_fns



using wire_id = uint32_t;

using SignalArray = std::vector<bool>;

constexpr wire_id WIRE_CONST_0 = std::numeric_limits<wire_id>::max() - 1;
constexpr wire_id WIRE_CONST_1 = std::numeric_limits<wire_id>::max();

inline std::vector<bool> evaluate_gate(const std::string& type, const std::string& raw_name,
                                        const std::vector<bool>& in) {
    const auto& table = gate_fns::table();
    auto it = table.find(type);
    if (it == table.end()) {
        throw std::runtime_error("No boolean function defined for cell type '" + type + "' (raw: '" + raw_name + "')");
    }
    return it->second(in);
}



struct GateData {
    std::string name;
    std::string type;

    std::vector<bool> evaluate(const std::vector<bool>& in) const {
        return evaluate_gate(type, name, in);
    }
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

#endif // TYPES_H