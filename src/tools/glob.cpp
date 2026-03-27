#include "glob.h"
#include <cstdio>
#include <array>
#include <sstream>

namespace opencodecpp {

nlohmann::json GlobTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {{"type", "string"}, {"description", "Glob pattern (e.g. **/*.cpp)"}}},
            {"path", {{"type", "string"}, {"description", "Base directory to search from"}, {"default", "."}}}
        }},
        {"required", {"pattern"}}
    };
}

nlohmann::json GlobTool::execute(const nlohmann::json& params) {
    std::string pattern = params.value("pattern", "");
    std::string basePath = params.value("path", ".");

    if (pattern.empty()) {
        return {{"error", "pattern parameter is required"}};
    }

    // Use find command to implement glob
    std::string cmd;
    // Convert glob pattern to find command
    if (pattern.find("**") != std::string::npos) {
        // Recursive glob
        std::string namePattern = pattern;
        // Extract the filename part after the last /
        size_t lastSlash = namePattern.rfind('/');
        if (lastSlash != std::string::npos) {
            namePattern = namePattern.substr(lastSlash + 1);
        }
        // Replace ** with nothing for the name pattern
        size_t starstar = namePattern.find("**");
        if (starstar != std::string::npos) {
            namePattern.erase(starstar, 2);
            if (namePattern.empty() || namePattern == "/") namePattern = "*";
        }
        cmd = "find " + basePath + " -name '" + namePattern + "' -type f 2>/dev/null | sort | head -1000";
    } else {
        cmd = "find " + basePath + " -name '" + pattern + "' -type f 2>/dev/null | sort | head -1000";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {{"error", "Failed to execute find command"}};
    }

    nlohmann::json files = nlohmann::json::array();
    std::array<char, 4096> buffer;
    std::string output;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            files.push_back(line);
        }
    }

    return {
        {"files", files},
        {"count", files.size()}
    };
}

} // namespace opencodecpp
