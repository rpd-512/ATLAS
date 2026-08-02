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

inline SignalArray evaluate_circuit(const Circuit& circuit, const SignalArray& input_values) {
    if (input_values.size() != circuit.inputs.size()) {
        throw std::runtime_error("evaluate_circuit: input_values size (" + std::to_string(input_values.size()) +
                                  ") does not match circuit.inputs size (" + std::to_string(circuit.inputs.size()) + ")");
    }

    std::unordered_map<wire_id, bool> input_map;
    input_map.reserve(circuit.inputs.size());
    for (size_t i = 0; i < circuit.inputs.size(); ++i) {
        input_map[circuit.inputs[i]] = input_values[i];
    }

    // wire -> (gate index, output pin index within that gate)
    std::unordered_map<wire_id, std::pair<size_t, size_t>> driver_of;
    for (size_t g = 0; g < circuit.gates.size(); ++g) {
        const auto& outs = circuit.gates[g].outputs;
        for (size_t p = 0; p < outs.size(); ++p) {
            driver_of[outs[p]] = {g, p};
        }
    }

    std::vector<bool> node_done(circuit.gates.size(), false);
    std::vector<char> node_visiting(circuit.gates.size(), false);   // combinational-loop guard
    std::vector<std::vector<bool>> node_result(circuit.gates.size());

    std::function<bool(wire_id)> value_of = [&](wire_id w) -> bool {
        if (w == WIRE_CONST_0) return false;
        if (w == WIRE_CONST_1) return true;

        auto in_it = input_map.find(w);
        if (in_it != input_map.end()) return in_it->second;

        auto drv_it = driver_of.find(w);
        if (drv_it == driver_of.end()) {
            throw std::runtime_error("evaluate_circuit: wire " + std::to_string(w) +
                                      " is neither a primary input nor any gate's output");
        }

        size_t gidx = drv_it->second.first;
        size_t pidx = drv_it->second.second;

        if (!node_done[gidx]) {
            if (node_visiting[gidx]) {
                throw std::runtime_error("evaluate_circuit: combinational loop through gate '" +
                                          circuit.gates[gidx].id + "'");
            }
            node_visiting[gidx] = true;

            const Gate& g = circuit.gates[gidx];
            std::vector<bool> args;
            args.reserve(g.inputs.size());
            for (wire_id in_w : g.inputs) {
                args.push_back(value_of(in_w));
            }
            node_result[gidx] = g.data.evaluate(args);

            node_visiting[gidx] = false;
            node_done[gidx] = true;
        }

        return node_result[gidx].at(pidx);
    };

    SignalArray result;
    result.reserve(circuit.outputs.size());
    for (wire_id w : circuit.outputs) {
        result.push_back(value_of(w));
    }
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
    size_t total_mismatches = 0;
    for (const auto& row : table.rows) {
        SignalArray actual_output = evaluate_circuit(circuit, row.inputs);
        total_mismatches += check_against_truth_table(table, row.inputs, actual_output);
    }
    return total_mismatches;
}

#endif // EVAL_UTILS_H