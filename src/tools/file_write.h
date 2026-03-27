#pragma once
#include "tool.h"

namespace opencodecpp {

class FileWriteTool : public Tool {
public:
    std::string name() const override { return "write"; }
    std::string description() const override {
        return "Write content to a file. Creates the file and any intermediate directories if they don't exist.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;
};

} // namespace opencodecpp
