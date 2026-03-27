#pragma once
#include "tool.h"

namespace opencodecpp {

class GlobTool : public Tool {
public:
    std::string name() const override { return "glob"; }
    std::string description() const override {
        return "Find files matching a glob pattern (e.g. **/*.cpp). Returns a list of matching file paths.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;
};

} // namespace opencodecpp
