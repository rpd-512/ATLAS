#ifndef ATLAS_UTILS_H
#define ATLAS_UTILS_H

#include "io_utils.h"
#include "area_utils.h"
#include "sta_utils.h"
#include "power_utils.h"
#include "eval_utils.h"
#include "soft_eval_utils.h"

inline void attach_liberty_data(Circuit& circuit, const LibertyLibrary& liberty,
                                 bool warn_missing = true) {
    circuit.nom_voltage = liberty.nom_voltage;   // global, from the liberty header

    for (Gate& g : circuit.gates) {
        auto it = liberty.cells_library.find(g.data.name);
        if (it != liberty.cells_library.end()) {
            const LibertyCellData& entry = it->second;
            g.data.area = static_cast<float>(entry.area);
            g.data.leakage_power = entry.leakage_power;
            g.data.liberty = &entry;

            size_t n = std::min(entry.input_capacitances.size(), g.data.input_capacitances.size());
            std::copy_n(entry.input_capacitances.begin(), n, g.data.input_capacitances.begin());

            // Compile each output pin's boolean function string once here,
            // rather than per-eval. compile_boolean_expr (eval_utils.h)
            // resolves variables by name at eval time against a
            // std::unordered_map<std::string, bool>, which GateData::evaluate
            // builds from entry.pin_index + the positional input vector.
            //
            // compile_soft_expr (soft_eval_utils.h) compiles the same
            // function string into the continuous/relaxed (product t-norm)
            // evaluator used by GateData::evaluate_soft. Both are compiled
            // from the same source string, in the same order, so
            // compiled_fns and compiled_soft_fns stay index-aligned with
            // entry.output_names.
            g.data.compiled_fns.clear();
            g.data.compiled_fns.reserve(entry.function_strings.size());
            g.data.compiled_soft_fns.clear();
            g.data.compiled_soft_fns.reserve(entry.function_strings.size());
            for (const std::string& fn_str : entry.function_strings) {
                g.data.compiled_fns.push_back(compile_boolean_expr(fn_str));
                g.data.compiled_soft_fns.push_back(compile_soft_expr(fn_str));
            }
        } else if (warn_missing) {
            std::cerr << "attach_liberty_data: no liberty entry for cell type '"
                       << g.data.name << "' (gate '" << g.id << "')\n";
        }
    }
}
#endif // ATLAS_UTILS_H