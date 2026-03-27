#include "file_edit.h"
#include <fstream>
#include <sstream>

namespace opencodecpp {

nlohmann::json FileEditTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"file_path", {{"type", "string"}, {"description", "Path to the file to edit"}}},
            {"old_string", {{"type", "string"}, {"description", "The string to find and replace"}}},
            {"new_string", {{"type", "string"}, {"description", "The replacement string"}}}
        }},
        {"required", {"file_path", "old_string", "new_string"}}
    };
}

nlohmann::json FileEditTool::execute(const nlohmann::json& params) {
    std::string filePath = params.value("file_path", "");
    std::string oldStr = params.value("old_string", "");
    std::string newStr = params.value("new_string", "");

    if (filePath.empty()) {
        return {{"error", "file_path parameter is required"}};
    }
    if (oldStr.empty()) {
        return {{"error", "old_string parameter is required"}};
    }

    // Read the file
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        return {{"error", "Cannot open file: " + filePath}};
    }

    std::ostringstream ss;
    ss << inFile.rdbuf();
    std::string content = ss.str();
    inFile.close();

    // Find and replace
    size_t pos = content.find(oldStr);
    if (pos == std::string::npos) {
        return {{"error", "old_string not found in file: " + filePath}};
    }

    content.replace(pos, oldStr.length(), newStr);

    // Write back
    std::ofstream outFile(filePath);
    if (!outFile.is_open()) {
        return {{"error", "Cannot open file for writing: " + filePath}};
    }

    outFile << content;
    outFile.close();

    return {
        {"success", true},
        {"file_path", filePath}
    };
}

} // namespace opencodecpp
