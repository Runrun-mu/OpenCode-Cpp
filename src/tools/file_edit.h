#pragma once
#include "tool.h"

namespace opencodecpp {

class FileEditTool : public Tool {
public:
    std::string name() const override { return "edit"; }
    std::string description() const override {
        return "Edit a file by replacing old_string with new_string. The old_string must exist in the file.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;
};

} // namespace opencodecpp
