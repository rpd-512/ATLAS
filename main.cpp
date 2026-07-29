#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

#include "includes/atlas_utils.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <netlist.json> <liberty.lib>\n";
        return 1;
    }

    // same hardcoded pattern as the Python script's __main__
    SignalArray input_values = {0, 1, 0, 0, 1, 1, 1, 1};

    Circuit circuit = parse_netlist(argv[1]);
    LibertyLibrary liberty = parse_liberty(argv[2]);
    attach_liberty_data(circuit, liberty);

    if (input_values.size() != circuit.inputs.size()) {
        std::cerr << "Netlist has " << circuit.inputs.size() << " input bit(s), "
                  << "but the hardcoded input_values here has " << input_values.size()
                  << " -- edit input_values in test.cpp to match.\n";
        return 1;
    }

    auto bitstring = [](const SignalArray& s) {
        std::string out;
        for (bool b : s) out += (b ? '1' : '0');
        return out;
    };

    std::cout << "Input values: " << bitstring(input_values) << "\n";

    std::reverse(input_values.begin(), input_values.end());   // matches input_values[::-1] in the Python
    auto t0 = std::chrono::steady_clock::now();
    SignalArray result = evaluate_circuit(circuit, input_values);
    auto t1 = std::chrono::steady_clock::now();
    std::reverse(result.begin(), result.end());                // matches result[::-1] in the Python

    std::cout << "Output values: " << bitstring(result) << "\n";

    auto us = [](auto d) { return std::chrono::duration_cast<std::chrono::microseconds>(d).count(); };
    std::cout << "evaluate_circuit: " << us(t1 - t0) << " us\n";
    std::vector<std::string> missing;

    std::cout << "total area:       " << compute_total_area(circuit) << " units\n";

    if (!missing.empty()) {
        std::cerr << missing.size() << " gates had no area data\n";
    }
    return 0;
}