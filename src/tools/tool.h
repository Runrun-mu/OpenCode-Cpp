#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace opencodecpp {

class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual nlohmann::json schema() const = 0;
    virtual nlohmann::json execute(const nlohmann::json& params) = 0;
};

} // namespace opencodecpp
