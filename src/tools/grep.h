#pragma once
#include "tool.h"

namespace opencodecpp {

class GrepTool : public Tool {
public:
    std::string name() const override { return "grep"; }
    std::string description() const override {
        return "Search file contents using a regular expression pattern. Returns matching filenames and line content.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;
};

} // namespace opencodecpp
