#ifndef EVAL_UTILS_H
#define EVAL_UTILS_H

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cctype>

#include "types.h"



// AST node base. Uses shared_ptr (not unique_ptr) for children so the
// compiled tree is copyable -- required for it to live inside a
// std::function, which needs its target to be copy-constructible.
struct BoolExpr {
    virtual ~BoolExpr() = default;
    virtual bool eval(const std::unordered_map<std::string, bool>& vars) const = 0;
};
using BoolExprPtr = std::shared_ptr<BoolExpr>;

struct VarExpr : BoolExpr {
    std::string name;
    explicit VarExpr(std::string n) : name(std::move(n)) {}
    bool eval(const std::unordered_map<std::string, bool>& vars) const override {
        auto it = vars.find(name);
        if (it == vars.end())
            throw std::runtime_error("compile_boolean_expr: unbound variable '" + name + "'");
        return it->second;
    }
};

struct NotExpr : BoolExpr {
    BoolExprPtr child;
    explicit NotExpr(BoolExprPtr c) : child(std::move(c)) {}
    bool eval(const std::unordered_map<std::string, bool>& vars) const override {
        return !child->eval(vars);
    }
};

struct AndExpr : BoolExpr {
    BoolExprPtr lhs, rhs;
    AndExpr(BoolExprPtr l, BoolExprPtr r) : lhs(std::move(l)), rhs(std::move(r)) {}
    bool eval(const std::unordered_map<std::string, bool>& vars) const override {
        return lhs->eval(vars) && rhs->eval(vars);
    }
};

struct OrExpr : BoolExpr {
    BoolExprPtr lhs, rhs;
    OrExpr(BoolExprPtr l, BoolExprPtr r) : lhs(std::move(l)), rhs(std::move(r)) {}
    bool eval(const std::unordered_map<std::string, bool>& vars) const override {
        return lhs->eval(vars) || rhs->eval(vars);
    }
};

// Recursive-descent parser.
//   expr   := term ('|' term)*
//   term   := factor ('&' factor)*
//   factor := '!' factor | '(' expr ')' | IDENT
class BoolExprParser {
public:
    explicit BoolExprParser(const std::string& s) : s_(s) {}

    BoolExprPtr parse() {
        auto e = parse_expr();
        skip_ws();
        if (pos_ != s_.size())
            throw std::runtime_error(
                "compile_boolean_expr: unexpected trailing input at position " + std::to_string(pos_));
        return e;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    bool match(char c) {
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    BoolExprPtr parse_expr() {
        auto lhs = parse_term();
        while (match('|')) lhs = std::make_shared<OrExpr>(lhs, parse_term());
        return lhs;
    }

    BoolExprPtr parse_term() {
        auto lhs = parse_factor();
        while (match('&')) lhs = std::make_shared<AndExpr>(lhs, parse_factor());
        return lhs;
    }

    BoolExprPtr parse_factor() {
        if (match('!')) return std::make_shared<NotExpr>(parse_factor());
        if (match('(')) {
            auto e = parse_expr();
            if (!match(')'))
                throw std::runtime_error("compile_boolean_expr: expected ')' at position " + std::to_string(pos_));
            return e;
        }
        return parse_ident();
    }

    BoolExprPtr parse_ident() {
        skip_ws();
        size_t start = pos_;
        if (pos_ >= s_.size() || !(std::isalpha(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
            throw std::runtime_error("compile_boolean_expr: expected variable name at position " + std::to_string(pos_));
        ++pos_;
        while (pos_ < s_.size() &&
               (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) {
            ++pos_;
        }
        return std::make_shared<VarExpr>(s_.substr(start, pos_ - start));
    }
};

// Compiles a boolean expression string ("&", "|", "!", parens, and
// variable names of any length) into a reusable evaluator. Parsing
// happens once; the returned function just walks the pre-built AST.
inline std::function<bool(const std::unordered_map<std::string, bool>&)>
compile_boolean_expr(const std::string& expr) {
    BoolExprPtr ast = BoolExprParser(expr).parse();
    return [ast](const std::unordered_map<std::string, bool>& vars) -> bool {
        return ast->eval(vars);
    };
}
struct CompiledCircuit {
    // dense wire ids: 0..num_wires-1
    std::unordered_map<wire_id, int> wire_index;      // built once, not used in hot path
    std::vector<int> input_slot;                       // circuit.inputs[i] -> dense idx
    std::vector<int> output_slot;                       // circuit.outputs[i] -> dense idx

    // gates in topological order, ready to evaluate front-to-back
    struct GateOp {
        const GateData* data;                           // pointer into original circuit
        std::vector<int> arg_slots;                      // dense indices of inputs
        std::vector<int> out_slots;                       // dense indices of outputs
    };
    std::vector<GateOp> topo_gates;

    int num_wires = 0;
};

inline CompiledCircuit compile_circuit(const Circuit& circuit) {
    CompiledCircuit cc;

    auto dense_id = [&](wire_id w) -> int {
        auto it = cc.wire_index.find(w);
        if (it != cc.wire_index.end()) return it->second;
        int idx = cc.num_wires++;
        cc.wire_index.emplace(w, idx);
        return idx;
    };

    // constants get fixed slots
    dense_id(WIRE_CONST_0);
    dense_id(WIRE_CONST_1);

    cc.input_slot.resize(circuit.inputs.size());
    for (size_t i = 0; i < circuit.inputs.size(); ++i)
        cc.input_slot[i] = dense_id(circuit.inputs[i]);

    std::unordered_map<wire_id, std::pair<size_t,size_t>> driver_of;
    for (size_t g = 0; g < circuit.gates.size(); ++g)
        for (size_t p = 0; p < circuit.gates[g].outputs.size(); ++p)
            driver_of[circuit.gates[g].outputs[p]] = {g, p};

    // iterative topo sort via DFS (post-order), same loop detection as before
    std::vector<char> state(circuit.gates.size(), 0); // 0=unvisited,1=visiting,2=done
    std::vector<size_t> order;
    order.reserve(circuit.gates.size());

    std::function<void(size_t)> visit = [&](size_t g) {
        if (state[g] == 2) return;
        if (state[g] == 1)
            throw std::runtime_error("evaluate_circuit: combinational loop through gate '" +
                                      circuit.gates[g].id + "'");
        state[g] = 1;
        for (wire_id in_w : circuit.gates[g].inputs) {
            if (in_w == WIRE_CONST_0 || in_w == WIRE_CONST_1) continue;
            bool is_input = std::find(circuit.inputs.begin(), circuit.inputs.end(), in_w) != circuit.inputs.end();
            if (is_input) continue;
            auto it = driver_of.find(in_w);
            if (it == driver_of.end())
                throw std::runtime_error("evaluate_circuit: wire " + std::to_string(in_w) +
                                          " is neither a primary input nor any gate's output");
            visit(it->second.first);
        }
        state[g] = 2;
        order.push_back(g);
    };
    for (size_t g = 0; g < circuit.gates.size(); ++g) visit(g);

    cc.topo_gates.reserve(order.size());
    for (size_t g : order) {
        CompiledCircuit::GateOp op;
        op.data = &circuit.gates[g].data;
        op.arg_slots.reserve(circuit.gates[g].inputs.size());
        for (wire_id w : circuit.gates[g].inputs) op.arg_slots.push_back(dense_id(w));
        op.out_slots.reserve(circuit.gates[g].outputs.size());
        for (wire_id w : circuit.gates[g].outputs) op.out_slots.push_back(dense_id(w));
        cc.topo_gates.push_back(std::move(op));
    }

    cc.output_slot.resize(circuit.outputs.size());
    for (size_t i = 0; i < circuit.outputs.size(); ++i)
        cc.output_slot[i] = dense_id(circuit.outputs[i]);

    return cc;
}

// Hot path — call this per input vector, as many times as you want.
inline SignalArray evaluate_compiled(const CompiledCircuit& cc, const SignalArray& input_values) {
    if (input_values.size() != cc.input_slot.size())
        throw std::runtime_error("evaluate_compiled: input size mismatch");

    std::vector<uint8_t> wire(cc.num_wires);   // uint8_t, not vector<bool> — avoids bitset overhead
    // constants: slot 0/1 reserved by construction order above if you want, or just set explicitly
    wire[cc.wire_index.at(WIRE_CONST_0)] = 0;   // fine to leave — precompute a template `wire` array once per CompiledCircuit if you want to skip this
    wire[cc.wire_index.at(WIRE_CONST_1)] = 1;

    for (size_t i = 0; i < cc.input_slot.size(); ++i)
        wire[cc.input_slot[i]] = static_cast<uint8_t>(input_values[i]);

    std::vector<bool> args_buf; // reused scratch, avoid realloc per gate
    for (const auto& op : cc.topo_gates) {
        args_buf.resize(op.arg_slots.size());
        for (size_t i = 0; i < op.arg_slots.size(); ++i)
            args_buf[i] = wire[op.arg_slots[i]];
        std::vector<bool> outs = op.data->evaluate(args_buf); // still calls your existing gate eval
        for (size_t p = 0; p < op.out_slots.size(); ++p)
            wire[op.out_slots[p]] = outs[p];
    }

    SignalArray result;
    result.reserve(cc.output_slot.size());
    for (int s : cc.output_slot) result.push_back(wire[s]);
    return result;
}


// ---------------------------------------------------------------------
// Truth-table checking
// ---------------------------------------------------------------------

// One row of a truth table: an input assignment together with the output
// that assignment is expected to produce.
struct TruthTableRow {
    SignalArray inputs;
    SignalArray expected_outputs;
};

// A truth table is just an ordered list of rows. It doesn't have to cover
// every 2^n input combination -- it's whatever rows you populate -- but
// for a true exhaustive check you'd give it one row per input combination.
struct TruthTable {
    std::vector<TruthTableRow> rows;
};

// Finds the row in `table` whose inputs match `input`, then compares
// `actual_output` (e.g. whatever evaluate_circuit produced for that input)
// against that row's expected_outputs, bit by bit.
// Returns the number of output bits that differ; 0 means an exact match.
inline size_t check_against_truth_table(const TruthTable& table,
                                         const SignalArray& input,
                                         const SignalArray& actual_output) {
    for (const auto& row : table.rows) {
        if (row.inputs == input) {
            if (actual_output.size() != row.expected_outputs.size()) {
                throw std::runtime_error(
                    "check_against_truth_table: actual_output size (" + std::to_string(actual_output.size()) +
                    ") does not match expected_outputs size (" + std::to_string(row.expected_outputs.size()) + ")");
            }
            size_t mismatches = 0;
            for (size_t i = 0; i < actual_output.size(); ++i) {
                if (actual_output[i] != row.expected_outputs[i]) ++mismatches;
            }
            return mismatches;
        }
    }
    throw std::runtime_error("check_against_truth_table: no row in the truth table matches the given input");
}

// Exhaustively walks every row of `table`, evaluates `circuit` on that
// row's input, and checks the result against the row's expected output.
// Returns the total number of output bits that differed, summed across
// every row -- 0 means the circuit matches the truth table exactly.
inline size_t check_truth_table_exhaustive(const Circuit& circuit, const TruthTable& table) {
    CompiledCircuit cc = compile_circuit(circuit);   // one-time cost

    size_t total_mismatches = 0;
    for (const auto& row : table.rows) {
        SignalArray actual_output = evaluate_compiled(cc, row.inputs);
        total_mismatches += check_against_truth_table(table, row.inputs, actual_output);
    }
    return total_mismatches;
}

#endif // EVAL_UTILS_H