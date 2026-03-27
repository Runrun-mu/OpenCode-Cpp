#pragma once
#include <string>
#include <vector>

namespace opencodecpp {

struct Skill {
    std::string name;
    std::string description;
    std::vector<std::string> globs;
    std::string content;   // markdown body
    std::string filePath;  // source file
    bool active = false;
};

class SkillManager {
public:
    // Discover skills from default directories (~/.opencode/skills and .opencode/skills)
    void discover();

    // Discover skills from a specific directory
    void discoverFrom(const std::string& dir);

    // Get all discovered skills
    std::vector<Skill>& getAllSkills();

    // Find a skill by name (returns nullptr if not found)
    Skill* findByName(const std::string& name);

    // Activate a skill by name (returns true if found and activated)
    bool activate(const std::string& name);

    // Auto-activate skills based on files in the given directory
    void autoActivate(const std::string& workDir);

    // Get combined prompt text for all active skills
    std::string getActiveSkillPrompt() const;

private:
    // Parse a single skill file
    Skill parseSkillFile(const std::string& path);

    std::vector<Skill> skills_;
};

} // namespace opencodecpp
