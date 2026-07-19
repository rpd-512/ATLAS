#include "includes/atlas_utils.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <file.sv> <top_module> <liberty_file>\n";
        return 1;
    }

    try {
        Atlas atlas(argv[1], argv[2], argv[3]);
        atlas.evaluate();
        atlas.save("output.json");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}