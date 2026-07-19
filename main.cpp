#include "includes/atlas_utils.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                   << " <file.sv> <top_module> <liberty_file> [clk_port] [clk_period_ns]\n";
        return 1;
    }

    std::string lef_path = "/OpenROAD/test/sky130hd/sky130_fd_sc_hd_merged.lef";
    std::string clk_port = (argc >= 5) ? argv[4] : "clk";
    double clk_period = (argc >= 6) ? std::stod(argv[5]) : 10.0;

    try {
        Atlas atlas(argv[1], argv[2], argv[3], lef_path, clk_port, clk_period);
        atlas.evaluate();
        atlas.save("output.json");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
