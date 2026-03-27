#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace opencodecpp {

// Utility JSON helpers
inline std::string jsonToString(const nlohmann::json& j) {
    if (j.is_string()) return j.get<std::string>();
    return j.dump();
}

} // namespace opencodecpp
