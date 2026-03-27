#include "skill.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <fnmatch.h>
#include <dirent.h>
#include <cstdlib>

namespace fs = std::filesystem;

namespace opencodecpp {

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Parse YAML-like globs field: either inline array ["*.py"] or multi-line - "*.py"
static std::vector<std::string> parseGlobs(const std::string& value, std::istringstream& stream, std::string& nextLine) {
    std::vector<std::string> globs;
    std::string trimmed = trim(value);

    // Inline array format: ["*.py", "**/*.py"]
    if (trimmed.size() >= 2 && trimmed[0] == '[') {
        // Remove brackets
        std::string inner = trimmed.substr(1, trimmed.size() - 2);
        std::istringstream ss(inner);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = trim(item);
            // Remove quotes
            if (item.size() >= 2 && (item[0] == '"' || item[0] == '\'')) {
                item = item.substr(1, item.size() - 2);
            }
            if (!item.empty()) {
                globs.push_back(item);
            }
        }
        return globs;
    }

    // Multi-line format:
    // globs:
    //   - "*.py"
    while (std::getline(stream, nextLine)) {
        std::string t = trim(nextLine);
        if (t.size() >= 2 && t[0] == '-') {
            std::string glob = trim(t.substr(1));
            // Remove quotes
            if (glob.size() >= 2 && (glob[0] == '"' || glob[0] == '\'')) {
                glob = glob.substr(1, glob.size() - 2);
            }
            if (!glob.empty()) {
                globs.push_back(glob);
            }
        } else {
            // Not a list item, we've gone past the globs
            break;
        }
        nextLine.clear();
    }

    return globs;
}

Skill SkillManager::parseSkillFile(const std::string& path) {
    Skill skill;
    skill.filePath = path;

    std::ifstream file(path);
    if (!file.is_open()) return skill;

    std::string fileContent((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    file.close();

    // Find frontmatter delimiters
    if (fileContent.substr(0, 3) != "---") return skill;

    size_t secondDelim = fileContent.find("\n---", 3);
    if (secondDelim == std::string::npos) return skill;

    std::string frontmatter = fileContent.substr(3, secondDelim - 3);
    // Body starts after the second ---\n
    size_t bodyStart = secondDelim + 4; // skip \n---
    if (bodyStart < fileContent.size() && fileContent[bodyStart] == '\n') bodyStart++;
    skill.content = (bodyStart < fileContent.size()) ? fileContent.substr(bodyStart) : "";

    // Parse frontmatter fields
    std::istringstream fmStream(frontmatter);
    std::string line;
    std::string pendingLine;

    while (true) {
        if (!pendingLine.empty()) {
            line = pendingLine;
            pendingLine.clear();
        } else if (!std::getline(fmStream, line)) {
            break;
        }

        std::string trimLine = trim(line);
        if (trimLine.empty()) continue;

        size_t colonPos = trimLine.find(':');
        if (colonPos == std::string::npos) continue;

        std::string key = trim(trimLine.substr(0, colonPos));
        std::string value = trim(trimLine.substr(colonPos + 1));

        if (key == "name") {
            skill.name = value;
        } else if (key == "description") {
            skill.description = value;
        } else if (key == "globs") {
            skill.globs = parseGlobs(value, fmStream, pendingLine);
        }
    }

    return skill;
}

void SkillManager::discoverFrom(const std::string& dir) {
    try {
        if (!fs::exists(dir) || !fs::is_directory(dir)) return;

        for (auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".md") {
                try {
                    Skill skill = parseSkillFile(entry.path().string());
                    if (!skill.name.empty()) {
                        // Don't add duplicates
                        bool exists = false;
                        for (auto& s : skills_) {
                            if (s.name == skill.name) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            skills_.push_back(skill);
                        }
                    }
                } catch (...) {
                    // Skip invalid files
                }
            }
        }
    } catch (...) {
        // Directory doesn't exist or can't be read
    }
}

void SkillManager::discover() {
    // Global skills: ~/.opencode/skills/
    const char* home = std::getenv("HOME");
    if (home) {
        std::string globalDir = std::string(home) + "/.opencode/skills";
        discoverFrom(globalDir);
    }

    // Project skills: .opencode/skills/
    discoverFrom(".opencode/skills");
}

std::vector<Skill>& SkillManager::getAllSkills() {
    return skills_;
}

Skill* SkillManager::findByName(const std::string& name) {
    for (auto& skill : skills_) {
        if (skill.name == name) return &skill;
    }
    return nullptr;
}

bool SkillManager::activate(const std::string& name) {
    Skill* skill = findByName(name);
    if (!skill) return false;
    skill->active = true;
    return true;
}

void SkillManager::autoActivate(const std::string& workDir) {
    // List files in workDir
    std::vector<std::string> files;
    try {
        for (auto& entry : fs::directory_iterator(workDir)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().filename().string());
            }
        }
    } catch (...) {
        return;
    }

    // Check each skill's globs against the files
    for (auto& skill : skills_) {
        for (auto& glob : skill.globs) {
            bool matched = false;
            for (auto& file : files) {
                if (fnmatch(glob.c_str(), file.c_str(), 0) == 0) {
                    matched = true;
                    break;
                }
            }
            if (matched) {
                skill.active = true;
                break;
            }
        }
    }
}

std::string SkillManager::getActiveSkillPrompt() const {
    std::string prompt;
    for (auto& skill : skills_) {
        if (skill.active) {
            prompt += "\n\n--- Skill: " + skill.name + " ---\n" + skill.content;
        }
    }
    return prompt;
}

} // namespace opencodecpp
