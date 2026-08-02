#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "includes/atlas_utils.h"
#include "includes/debug_utils.h"

// LOD8 - 8-bit leading-one detector.
// row.inputs[i]  : input bit i, right = 0, left = 7
// row.expected_outputs[i] : bit i of the 3-bit index (0 = LSB), i in [0,2]
// Scan direction is right -> left; output = index of the first 1 found,
// i.e. the position of the lowest set input bit.
// ASSUMPTION: input 00000000 -> expected output 000 (see note above).
TruthTable make_lod8_truth_table() {
    TruthTable table;
    table.rows.reserve(256);

    for (unsigned v = 0; v < 256; ++v) {
        TruthTableRow row;

        // inputs[i] corresponds to bit i
        row.inputs.resize(8);
        for (int i = 0; i < 8; ++i) {
            row.inputs[i] = (v >> i) & 1u;
        }

        // Find the leading 1 (highest set bit)
        unsigned idx = 0;

        for (int i = 7; i >= 0; --i) {
            if ((v >> i) & 1u) {
                idx = static_cast<unsigned>(i);
                break;
            }
        }

        // Encode index as 3-bit binary
        row.expected_outputs.resize(3);
        for (int i = 0; i < 3; ++i) {
            row.expected_outputs[i] = (idx >> i) & 1u;
        }

        table.rows.push_back(std::move(row));
    }

    return table;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <netlist.json> <liberty.lib>\n";
        return 1;
    }

    Circuit circuit = parse_netlist(argv[1]);
    LibertyLibrary liberty = parse_liberty(argv[2]);
    attach_liberty_data(circuit, liberty);

    SignalArray input_values = {0, 1, 0, 0,
                                 1, 1, 1, 1};

    if (input_values.size() != circuit.inputs.size()) {
        std::cerr << "Netlist has " << circuit.inputs.size() << " input bit(s), "
                  << "but the hardcoded input_values here has " << input_values.size()
                  << " -- edit input_values in test.cpp to match.\n";
        return 1;
    }

    SoftSignalArray soft_input_values = {0.5, 0.5, 0.5, 0.5,
                                          0.5, 0.5, 0.5, 0.5};

    if (soft_input_values.size() != circuit.inputs.size()) {
        std::cerr << "Netlist has " << circuit.inputs.size() << " input bit(s), "
                  << "but the hardcoded soft_input_values here has " << soft_input_values.size()
                  << " -- edit soft_input_values in test.cpp to match.\n";
        return 1;
    }

    CompiledCircuit cc = compile_circuit(circuit);   // add this before the timed section

    std::reverse(input_values.begin(), input_values.end());
    Clock::time_point t0 = Clock::now();
    SignalArray result = evaluate_compiled(cc, input_values);
    Clock::time_point t1 = Clock::now();
    std::reverse(result.begin(), result.end());
    std::reverse(input_values.begin(), input_values.end());
    double eval_time_us = elapsed_us(t0, t1);

    std::vector<std::string> missing_area;
    Clock::time_point t2 = Clock::now();
    float total_area = compute_total_area(circuit, &missing_area);
    Clock::time_point t3 = Clock::now();
    double area_time_us = elapsed_us(t2, t3);

    Clock::time_point t4 = Clock::now();
    run_sta(circuit);
    run_sta_arrival(circuit);
    run_sta_required(circuit);
    run_sta_slack(circuit);
    Clock::time_point t5 = Clock::now();
    double sta_time_us = elapsed_us(t4, t5);

    Clock::time_point t6 = Clock::now();
    float static_power = compute_static_power(circuit);
    Clock::time_point t7 = Clock::now();
    double static_power_time_us = elapsed_us(t6, t7);

    std::reverse(soft_input_values.begin(), soft_input_values.end());
    Clock::time_point ts0 = Clock::now();
    SoftSignalArray soft_result = evaluate_circuit_soft(circuit, soft_input_values);
    Clock::time_point ts1 = Clock::now();
    std::reverse(soft_result.begin(), soft_result.end());
    std::reverse(soft_input_values.begin(), soft_input_values.end());
    double soft_eval_time_us = elapsed_us(ts0, ts1);

    std::vector<std::string> missing_switching;
    Clock::time_point tp0 = Clock::now();
    float switching_power = compute_switching_power(circuit, &missing_switching);
    Clock::time_point tp1 = Clock::now();
    double switching_power_time_us = elapsed_us(tp0, tp1);

    std::vector<std::string> missing_internal;
    Clock::time_point ti0 = Clock::now();
    float internal_power = compute_internal_power(circuit, &missing_internal);
    Clock::time_point ti1 = Clock::now();
    double internal_power_time_us = elapsed_us(ti0, ti1);

    float worst_arrival = 0.0f;
    std::string critical_gate;
    for (const Gate& g : circuit.gates) {
        if (g.data.arrival_time > worst_arrival) {
            worst_arrival = g.data.arrival_time;
            critical_gate = g.id;
        }
    }

    TruthTable lod8_table = make_lod8_truth_table();
    Clock::time_point tt0 = Clock::now();
    size_t total_mismatches = check_truth_table_exhaustive(circuit, lod8_table);
    Clock::time_point tt1 = Clock::now();
    double lod8_time_us = elapsed_us(tt0, tt1);
    //print_truth_table(lod8_table, 8, 3);


    print_section("Functional evaluation");
    std::cout << "input values:  " << to_bitstring(input_values) << "\n";
    std::cout << "output values: " << to_bitstring(result) << "\n";

    print_section("Soft evaluation (product t-norm)");
    std::cout << "soft input values:  " << to_soft_string(soft_input_values) << "\n";
    std::cout << "soft output values: " << to_soft_string(soft_result) << "\n";

    print_section("Evaluating Circuit Against Truth Table");
    std::cout << "rows checked:     " << lod8_table.rows.size() << "\n";
    std::cout << "total mismatches: " << total_mismatches
            << " (output bits summed across all rows)\n";


    print_section("Area");
    std::cout << "total area: " << total_area << " units\n";
    if (!missing_area.empty()) {
        std::cout << "gates missing area data: " << missing_area.size() << "\n";
    }

    print_section("Static timing analysis");
    std::cout << "critical path delay: " << worst_arrival << " ns\n";
    std::cout << "critical gate:       " << critical_gate << "\n";

    print_section("Power");
    std::cout << "static power:    " << static_power << " W\n";
    std::cout << "switching power: " << switching_power << " W\n";
    std::cout << "internal power:  " << internal_power << " W\n";
    std::cout << "total power:     " << (static_power + switching_power + internal_power) << " W\n";
    if (!missing_switching.empty()) {
        std::cout << "gates missing switching power inputs: " << missing_switching.size() << "\n";
    }
    if (!missing_internal.empty()) {
        std::cout << "gates missing internal power inputs:  " << missing_internal.size() << "\n";
    }

    print_section("Timing breakdown");
    std::cout << "evaluate_circuit:        " << eval_time_us << " us\n";
    std::cout << "compute_total_area:      " << area_time_us << " us\n";
    std::cout << "STA (full pipeline):     " << sta_time_us << " us\n";
    std::cout << "compute_static_power:    " << static_power_time_us << " us\n";
    std::cout << "evaluate_circuit_soft:   " << soft_eval_time_us << " us\n";
    std::cout << "compute_switching_power: " << switching_power_time_us << " us\n";
    std::cout << "compute_internal_power:  " << internal_power_time_us << " us\n";
    std::cout << "check_truth_table:       " << lod8_time_us << " us\n";

    double total_time_us = eval_time_us + area_time_us + sta_time_us + static_power_time_us +
                            soft_eval_time_us + switching_power_time_us + internal_power_time_us;
    std::cout << "total:                   " << total_time_us + lod8_time_us << " us\n";

    return 0;
}