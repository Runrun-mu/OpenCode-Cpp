#pragma once
#include "tool.h"

namespace opencodecpp {

class BashTool : public Tool {
public:
    std::string name() const override { return "bash"; }
    std::string description() const override {
        return "Execute a shell command and return stdout and stderr. Supports timeout parameter (default 30s).";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;
};

} // namespace opencodecpp
