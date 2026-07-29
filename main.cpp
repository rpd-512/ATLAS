#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

#include "includes/atlas_utils.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <netlist.json>\n";
        return 1;
    }

    // same hardcoded pattern as the Python script's __main__
    SignalArray input_values = {0, 1, 0, 0, 1, 1, 1, 1};

    auto t0 = std::chrono::steady_clock::now();
    Circuit circuit = parse_netlist(argv[1]);
    auto t1 = std::chrono::steady_clock::now();

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
    SignalArray result = evaluate_circuit(circuit, input_values);
    auto t2 = std::chrono::steady_clock::now();
    std::reverse(result.begin(), result.end());                // matches result[::-1] in the Python

    std::cout << "Output values: " << bitstring(result) << "\n";

    auto us = [](auto d) { return std::chrono::duration_cast<std::chrono::microseconds>(d).count(); };
    std::cout << "parse_netlist:    " << us(t1 - t0) << " us\n";
    std::cout << "evaluate_circuit: " << us(t2 - t1) << " us\n";
    std::cout << "total:            " << us(t2 - t0) << " us\n";
    return 0;
}