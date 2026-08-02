#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "includes/atlas_utils.h"

#include "includes/debug_utils.h"

using Clock = std::chrono::steady_clock;

double elapsed_us(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

std::string to_bitstring(const SignalArray& signals) {
    std::string out;
    out.reserve(signals.size());
    for (bool b : signals) out += (b ? '1' : '0');
    return out;
}

std::string to_soft_string(const SoftSignalArray& signals) {
    std::string out = "[";
    for (size_t i = 0; i < signals.size(); ++i) {
        if (i) out += ", ";
        out += std::to_string(signals[i]);
    }
    out += "]";
    return out;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <netlist.json> <liberty.lib>\n";
        return 1;
    }

    // same hardcoded pattern as the Python script's __main__
    SignalArray input_values = {0, 1, 0, 0,
                                 1, 1, 1, 1,};
    Circuit circuit = parse_netlist(argv[1]);
    LibertyLibrary liberty = parse_liberty(argv[2]);
    attach_liberty_data(circuit, liberty);
    //print_liberty_library(liberty);

    if (input_values.size() != circuit.inputs.size()) {
        std::cerr << "Netlist has " << circuit.inputs.size() << " input bit(s), "
                  << "but the hardcoded input_values here has " << input_values.size()
                  << " -- edit input_values in test.cpp to match.\n";
        return 1;
    }

    std::cout << "Input values: " << to_bitstring(input_values) << "\n";

    // --- evaluate_circuit ---
    std::reverse(input_values.begin(), input_values.end());  // matches input_values[::-1] in Python
    Clock::time_point t0 = Clock::now();
    SignalArray result = evaluate_circuit(circuit, input_values);
    Clock::time_point t1 = Clock::now();
    std::reverse(result.begin(), result.end());  // matches result[::-1] in Python

    double eval_time_us = elapsed_us(t0, t1);

    // --- evaluate_circuit_soft (continuous relaxation demo) ---
    // Independent, fractional input set -- these are *not* meant to match
    // input_values; the point is to exercise values strictly inside (0,1),
    // where the product t-norm relaxation actually diverges from boolean
    // logic (at the {0,1} corners it agrees with evaluate_circuit exactly).
    SoftSignalArray soft_input_values = {0.5,0.5,0.5,0.5,
                                         0.5,0.5,0.5,0.5};

    if (soft_input_values.size() != circuit.inputs.size()) {
        std::cerr << "Netlist has " << circuit.inputs.size() << " input bit(s), "
                  << "but the hardcoded soft_input_values here has " << soft_input_values.size()
                  << " -- edit soft_input_values in test.cpp to match.\n";
        return 1;
    }

    std::cout << "Soft input values: " << to_soft_string(soft_input_values) << "\n";

    std::reverse(soft_input_values.begin(), soft_input_values.end());  // same wire-order convention as input_values
    Clock::time_point ts0 = Clock::now();
    SoftSignalArray soft_result = evaluate_circuit_soft(circuit, soft_input_values);
    Clock::time_point ts1 = Clock::now();
    std::reverse(soft_result.begin(), soft_result.end());  // matches result[::-1] convention
    std::reverse(soft_input_values.begin(), soft_input_values.end());  // restore original order for printing

    double soft_eval_time_us = elapsed_us(ts0, ts1);

    // --- compute_total_area ---
    std::vector<std::string> missing;
    Clock::time_point t2 = Clock::now();
    float total_area = compute_total_area(circuit, &missing);
    Clock::time_point t3 = Clock::now();
    double area_time_us = elapsed_us(t2, t3);

    // --- run_sta + arrival/required/slack, timed as one STA pipeline ---
    Clock::time_point t4 = Clock::now();
    run_sta(circuit);
    run_sta_arrival(circuit);
    run_sta_required(circuit);
    run_sta_slack(circuit);
    Clock::time_point t5 = Clock::now();
    double sta_time_us = elapsed_us(t4, t5);

    // --- run power analysis ---
    Clock::time_point t6 = Clock::now();
    float static_power = compute_static_power(circuit);
    Clock::time_point t7 = Clock::now();
    double power_time_us = elapsed_us(t6, t7);

    std::cout << "Output values: " << to_bitstring(result) << "\n";
    std::cout << "total area:       " << total_area << " units\n";
    std::cout << "static power:     " << static_power << " W\n";

    std::cout << "\nSoft eval demo (continuous relaxation, product t-norm):\n";
    std::cout << "\tsoft input values:  " << to_soft_string(soft_input_values) << "\n";
    std::cout << "\tsoft output values: " << to_soft_string(soft_result) << "\n";

    std::cout << "\nPer-gate timing (AT / RT / Slack):\n";
    float worst_arrival = 0.0f;
    std::string critical_gate;
    for (const Gate& g : circuit.gates) {
        //std::cout << "\t" << g.id << " (" << g.data.type << "): "
        //          << "AT=" << g.data.arrival_time
        //          << " RT=" << g.data.required_time
        //          << " slack=" << g.data.slack << "\n";

        if (g.data.arrival_time > worst_arrival) {
            worst_arrival = g.data.arrival_time;
            critical_gate = g.id;
        }
    }

    std::cout << "\ncritical path propagation delay: " << worst_arrival
              << " ns (through " << critical_gate << ")\n";

    std::cout << "\nTime taken:\n";
    std::cout << "\tevaluate_circuit: " << eval_time_us << " us\n";
    std::cout << "\tevaluate_circuit_soft: " << soft_eval_time_us << " us\n";
    std::cout << "\tcompute_total_area: " << area_time_us << " us\n";
    std::cout << "\tSTA (run_sta + arrival + required + slack): " << sta_time_us << " us\n";
    std::cout << "\tcompute_static_power: " << power_time_us << " us\n";

    std::cout << "Total time: " << (eval_time_us + soft_eval_time_us + area_time_us + sta_time_us) << " us\n";

    if (!missing.empty()) {
        std::cerr << missing.size() << " gates had no area data\n";
    }

    //print_liberty_library(liberty);

    return 0;
}