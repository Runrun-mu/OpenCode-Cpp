#include "file_write.h"
#include <fstream>
#include <sys/stat.h>
#include <string>

namespace opencodecpp {

static void mkdirRecursive(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        mkdir(sub.c_str(), 0755);
    }
}

nlohmann::json FileWriteTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"file_path", {{"type", "string"}, {"description", "Path to the file to write"}}},
            {"content", {{"type", "string"}, {"description", "Content to write to the file"}}}
        }},
        {"required", {"file_path", "content"}}
    };
}

nlohmann::json FileWriteTool::execute(const nlohmann::json& params) {
    std::string filePath = params.value("file_path", "");
    std::string content = params.value("content", "");

    if (filePath.empty()) {
        return {{"error", "file_path parameter is required"}};
    }

    // Create intermediate directories
    mkdirRecursive(filePath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        return {{"error", "Cannot open file for writing: " + filePath}};
    }

    file << content;
    file.close();

    return {
        {"success", true},
        {"file_path", filePath},
        {"bytes_written", static_cast<int>(content.size())}
    };
}

} // namespace opencodecpp
