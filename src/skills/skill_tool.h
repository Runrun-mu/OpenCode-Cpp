#pragma once
#include "../tools/tool.h"
#include "skill.h"

namespace opencodecpp {

class SkillTool : public Tool {
public:
    explicit SkillTool(SkillManager& manager);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;

private:
    SkillManager& manager_;
};

} // namespace opencodecpp
