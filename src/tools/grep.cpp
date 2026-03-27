#include "grep.h"
#include <cstdio>
#include <array>
#include <sstream>

namespace opencodecpp {

nlohmann::json GrepTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {{"type", "string"}, {"description", "Regular expression pattern to search for"}}},
            {"path", {{"type", "string"}, {"description", "File or directory to search in"}, {"default", "."}}}
        }},
        {"required", {"pattern"}}
    };
}

nlohmann::json GrepTool::execute(const nlohmann::json& params) {
    std::string pattern = params.value("pattern", "");
    std::string path = params.value("path", ".");

    if (pattern.empty()) {
        return {{"error", "pattern parameter is required"}};
    }

    // Use grep -rn (or rg if available)
    std::string cmd = "grep -rn --include='*' '" + pattern + "' " + path + " 2>/dev/null | head -200";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {{"error", "Failed to execute grep command"}};
    }

    std::array<char, 4096> buffer;
    std::string output;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);

    nlohmann::json matches = nlohmann::json::array();
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            matches.push_back(line);
        }
    }

    return {
        {"matches", matches},
        {"count", matches.size()}
    };
}

} // namespace opencodecpp
