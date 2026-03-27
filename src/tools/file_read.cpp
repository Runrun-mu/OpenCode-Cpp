#include "file_read.h"
#include <fstream>
#include <sstream>

namespace opencodecpp {

nlohmann::json FileReadTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"file_path", {{"type", "string"}, {"description", "Path to the file to read"}}},
            {"offset", {{"type", "integer"}, {"description", "Starting line number (1-based)"}, {"default", 1}}},
            {"limit", {{"type", "integer"}, {"description", "Number of lines to read (0 = all)"}, {"default", 0}}}
        }},
        {"required", {"file_path"}}
    };
}

nlohmann::json FileReadTool::execute(const nlohmann::json& params) {
    std::string filePath = params.value("file_path", "");
    int offset = params.value("offset", 1);
    int limit = params.value("limit", 0);

    if (filePath.empty()) {
        return {{"error", "file_path parameter is required"}};
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        return {{"error", "Cannot open file: " + filePath}};
    }

    std::string line;
    std::ostringstream result;
    int lineNum = 0;
    int linesRead = 0;

    while (std::getline(file, line)) {
        lineNum++;
        if (lineNum < offset) continue;
        if (limit > 0 && linesRead >= limit) break;
        if (linesRead > 0) result << "\n";
        result << lineNum << "\t" << line;
        linesRead++;
    }

    return {
        {"content", result.str()},
        {"lines_read", linesRead},
        {"file_path", filePath}
    };
}

} // namespace opencodecpp
