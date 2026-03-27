#pragma once
#include "tool.h"

namespace opencodecpp {

class LsTool : public Tool {
public:
    std::string name() const override { return "ls"; }
    std::string description() const override {
        return "List files and directories at the given path.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;
};

} // namespace opencodecpp
