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

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <netlist.json> <liberty.lib>\n";
        return 1;
    }

    // same hardcoded pattern as the Python script's __main__
    SignalArray input_values = {0, 1, 0, 0, 1, 1, 1, 1};

    Circuit circuit = parse_netlist(argv[1]);
    LibertyLibrary liberty = parse_liberty(argv[2]);
    fill_liberty_test_defaults(liberty);   // TEMP: stub coeffs until parse_liberty extracts real tables
    attach_liberty_data(circuit, liberty);

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

    // --- add more timed steps here the same way ---

    std::cout << "Output values: " << to_bitstring(result) << "\n";
    std::cout << "total area:       " << total_area << " units\n";

    std::cout << "\nPer-gate timing (AT / RT / Slack):\n";
    float worst_arrival = 0.0f;
    std::string critical_gate;
    for (const Gate& g : circuit.gates) {
        std::cout << "\t" << g.id << " (" << g.data.type << "): "
                  << "AT=" << g.data.arrival_time
                  << " RT=" << g.data.required_time
                  << " slack=" << g.data.slack << "\n";

        if (g.data.arrival_time > worst_arrival) {
            worst_arrival = g.data.arrival_time;
            critical_gate = g.id;
        }
    }

    std::cout << "\ncritical path propagation delay: " << worst_arrival
              << " ns (through " << critical_gate << ")\n";

    std::cout << "\nTime taken:\n";
    std::cout << "\tevaluate_circuit: " << eval_time_us << " us\n";
    std::cout << "\tcompute_total_area: " << area_time_us << " us\n";
    std::cout << "\tSTA (run_sta + arrival + required + slack): " << sta_time_us << " us\n";

    std::cout << "Total time: " << (eval_time_us + area_time_us + sta_time_us) << " us\n";

    if (!missing.empty()) {
        std::cerr << missing.size() << " gates had no area data\n";
    }

    //print_liberty_library(liberty);

    return 0;
}