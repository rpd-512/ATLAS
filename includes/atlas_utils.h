#ifndef ATLAS_UTILS_H
#define ATLAS_UTILS_H

#include <string>
#include <unordered_map>
#include <optional>
#include <stdexcept>

#include "io_utils.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <stdexcept>

using wire_id = uint32_t;

using SignalArray = std::vector<bool>;

struct GateData {
    std::string name;
    std::string type;
    std::vector<bool> evaluate(const std::vector<bool>& inputs) const;    
};
struct Gate {
    std::string id;
    std::vector<wire_id> outputs;
    GateData data;
    std::vector<wire_id> inputs;
};

struct Circuit {
    std::vector<Gate> gates;
    SignalArray inputs;
};




#endif // ATLAS_UTILS_H