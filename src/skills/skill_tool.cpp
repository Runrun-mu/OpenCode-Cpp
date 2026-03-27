#include "skill_tool.h"

namespace opencodecpp {

SkillTool::SkillTool(SkillManager& manager) : manager_(manager) {}

std::string SkillTool::name() const {
    return "skill";
}

std::string SkillTool::description() const {
    return "List and load skill definitions";
}

nlohmann::json SkillTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"action", {
                {"type", "string"},
                {"enum", {"list", "get"}},
                {"description", "Action to perform: list all skills or get a specific skill"}
            }},
            {"name", {
                {"type", "string"},
                {"description", "Skill name (required for get action)"}
            }}
        }},
        {"required", {"action"}}
    };
}

nlohmann::json SkillTool::execute(const nlohmann::json& params) {
    std::string action = params.value("action", "");

    if (action == "list") {
        nlohmann::json skills = nlohmann::json::array();
        for (auto& skill : manager_.getAllSkills()) {
            skills.push_back({
                {"name", skill.name},
                {"description", skill.description},
                {"active", skill.active}
            });
        }
        return {{"skills", skills}};
    }

    if (action == "get") {
        std::string skillName = params.value("name", "");
        if (skillName.empty()) {
            return {{"error", "name parameter is required for get action"}};
        }
        Skill* skill = manager_.findByName(skillName);
        if (!skill) {
            return {{"error", "Skill not found: " + skillName}};
        }
        return {
            {"name", skill->name},
            {"description", skill->description},
            {"content", skill->content},
            {"active", skill->active}
        };
    }

    return {{"error", "Unknown action: " + action}};
}

} // namespace opencodecpp
