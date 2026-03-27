#include "ls.h"
#include <dirent.h>
#include <sys/stat.h>

namespace opencodecpp {

nlohmann::json LsTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Directory path to list"}, {"default", "."}}}
        }},
        {"required", {"path"}}
    };
}

nlohmann::json LsTool::execute(const nlohmann::json& params) {
    std::string path = params.value("path", ".");

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return {{"error", "Cannot open directory: " + path}};
    }

    nlohmann::json entries = nlohmann::json::array();
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        std::string fullPath = path + "/" + name;
        struct stat st;
        nlohmann::json item;
        item["name"] = name;

        if (stat(fullPath.c_str(), &st) == 0) {
            item["type"] = S_ISDIR(st.st_mode) ? "directory" : "file";
            item["size"] = st.st_size;
        } else {
            item["type"] = (entry->d_type == DT_DIR) ? "directory" : "file";
        }

        entries.push_back(item);
    }
    closedir(dir);

    return {
        {"entries", entries},
        {"path", path}
    };
}

} // namespace opencodecpp
