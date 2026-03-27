#pragma once
#include "tool.h"

namespace opencodecpp {

class FileReadTool : public Tool {
public:
    std::string name() const override { return "read"; }
    std::string description() const override {
        return "Read the contents of a file. Supports offset (starting line) and limit (number of lines) parameters.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;
};

} // namespace opencodecpp
