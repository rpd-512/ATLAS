#ifndef SOFT_EVAL_UTILS_H
#define SOFT_EVAL_UTILS_H

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cctype>
#include "types.h"

// AST node base for the probabilistic/continuous relaxation of boolean
// logic (product t-norm): AND = A*B, OR = A+B-A*B, NOT = 1-A. Values are
// expected in [0,1]; at the corners {0,1} this agrees exactly with
// standard boolean logic.
struct SoftExpr {
    virtual ~SoftExpr() = default;
    virtual float eval(const std::unordered_map<std::string, float>& vars) const = 0;
};
using SoftExprPtr = std::shared_ptr<SoftExpr>;

struct SoftVarExpr : SoftExpr {
    std::string name;
    explicit SoftVarExpr(std::string n) : name(std::move(n)) {}
    float eval(const std::unordered_map<std::string, float>& vars) const override {
        auto it = vars.find(name);
        if (it == vars.end())
            throw std::runtime_error("compile_soft_expr: unbound variable '" + name + "'");
        return it->second;
    }
};

struct SoftNotExpr : SoftExpr {
    SoftExprPtr child;
    explicit SoftNotExpr(SoftExprPtr c) : child(std::move(c)) {}
    float eval(const std::unordered_map<std::string, float>& vars) const override {
        return 1.0f - child->eval(vars);
    }
};

struct SoftAndExpr : SoftExpr {
    SoftExprPtr lhs, rhs;
    SoftAndExpr(SoftExprPtr l, SoftExprPtr r) : lhs(std::move(l)), rhs(std::move(r)) {}
    float eval(const std::unordered_map<std::string, float>& vars) const override {
        return lhs->eval(vars) * rhs->eval(vars);
    }
};

struct SoftOrExpr : SoftExpr {
    SoftExprPtr lhs, rhs;
    SoftOrExpr(SoftExprPtr l, SoftExprPtr r) : lhs(std::move(l)), rhs(std::move(r)) {}
    float eval(const std::unordered_map<std::string, float>& vars) const override {
        float a = lhs->eval(vars);
        float b = rhs->eval(vars);
        return a + b - a * b;
    }
};

// Recursive-descent parser -- identical grammar to BoolExprParser:
//   expr   := term ('|' term)*
//   term   := factor ('&' factor)*
//   factor := '!' factor | '(' expr ')' | IDENT
class SoftExprParser {
public:
    explicit SoftExprParser(const std::string& s) : s_(s) {}

    SoftExprPtr parse() {
        auto e = parse_expr();
        skip_ws();
        if (pos_ != s_.size())
            throw std::runtime_error(
                "compile_soft_expr: unexpected trailing input at position " + std::to_string(pos_));
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

    SoftExprPtr parse_expr() {
        auto lhs = parse_term();
        while (match('|')) lhs = std::make_shared<SoftOrExpr>(lhs, parse_term());
        return lhs;
    }

    SoftExprPtr parse_term() {
        auto lhs = parse_factor();
        while (match('&')) lhs = std::make_shared<SoftAndExpr>(lhs, parse_factor());
        return lhs;
    }

    SoftExprPtr parse_factor() {
        if (match('!')) return std::make_shared<SoftNotExpr>(parse_factor());
        if (match('(')) {
            auto e = parse_expr();
            if (!match(')'))
                throw std::runtime_error("compile_soft_expr: expected ')' at position " + std::to_string(pos_));
            return e;
        }
        return parse_ident();
    }

    SoftExprPtr parse_ident() {
        skip_ws();
        size_t start = pos_;
        if (pos_ >= s_.size() || !(std::isalpha(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
            throw std::runtime_error("compile_soft_expr: expected variable name at position " + std::to_string(pos_));
        ++pos_;
        while (pos_ < s_.size() &&
               (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) {
            ++pos_;
        }
        return std::make_shared<SoftVarExpr>(s_.substr(start, pos_ - start));
    }
};

// Compiles a boolean expression string ("&", "|", "!", parens, and
// variable names of any length) into a reusable *continuous* evaluator
// over [0,1]-valued inputs, using the product t-norm relaxation. Parsing
// happens once; the returned function just walks the pre-built AST.
inline std::function<float(const std::unordered_map<std::string, float>&)>
compile_soft_expr(const std::string& expr) {
    SoftExprPtr ast = SoftExprParser(expr).parse();
    return [ast](const std::unordered_map<std::string, float>& vars) -> float {
        return ast->eval(vars);
    };
}

// Static-probability switching-activity estimator: for a signal with
// probability p of being 1 (as produced by the product t-norm relaxation),
// the expected toggle rate under the standard independent-transitions
// model is 2*p*(1-p) -- 0 at the corners (p=0 or p=1, signal never
// switches), peaking at p=0.5 (maximally uncertain / most active).
inline float toggle_rate_from_prob(float p) {
    return 2.0f * p * (1.0f - p);
}

inline SoftSignalArray evaluate_circuit_soft(Circuit& circuit, const SoftSignalArray& input_values) {
    if (input_values.size() != circuit.inputs.size()) {
        throw std::runtime_error("evaluate_circuit_soft: input_values size (" + std::to_string(input_values.size()) +
                                  ") does not match circuit.inputs size (" + std::to_string(circuit.inputs.size()) + ")");
    }

    std::unordered_map<wire_id, float> input_map;
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
    std::vector<std::vector<float>> node_result(circuit.gates.size());

    std::function<float(wire_id)> value_of = [&](wire_id w) -> float {
        if (w == WIRE_CONST_0) return 0.0f;
        if (w == WIRE_CONST_1) return 1.0f;

        auto in_it = input_map.find(w);
        if (in_it != input_map.end()) return in_it->second;

        auto drv_it = driver_of.find(w);
        if (drv_it == driver_of.end()) {
            throw std::runtime_error("evaluate_circuit_soft: wire " + std::to_string(w) +
                                      " is neither a primary input nor any gate's output");
        }

        size_t gidx = drv_it->second.first;
        size_t pidx = drv_it->second.second;

        if (!node_done[gidx]) {
            if (node_visiting[gidx]) {
                throw std::runtime_error("evaluate_circuit_soft: combinational loop through gate '" +
                                          circuit.gates[gidx].id + "'");
            }
            node_visiting[gidx] = true;

            Gate& g = circuit.gates[gidx];
            std::vector<float> args;
            args.reserve(g.inputs.size());
            for (wire_id in_w : g.inputs) {
                args.push_back(value_of(in_w));
            }
            node_result[gidx] = g.data.evaluate_soft(args);

            // Derive per-output toggle rate from the soft-evaluated output
            // probabilities and cache it on the gate, same convention as
            // the STA passes writing arrival_time/slack in place.
            g.data.toggle_rate.clear();
            g.data.toggle_rate.reserve(node_result[gidx].size());
            for (float p : node_result[gidx]) {
                g.data.toggle_rate.push_back(toggle_rate_from_prob(p));
            }

            node_visiting[gidx] = false;
            node_done[gidx] = true;
        }

        return node_result[gidx].at(pidx);
    };

    SoftSignalArray result;
    result.reserve(circuit.outputs.size());
    for (wire_id w : circuit.outputs) {
        result.push_back(value_of(w));
    }
    return result;
}


#endif // SOFT_EVAL_UTILS_H