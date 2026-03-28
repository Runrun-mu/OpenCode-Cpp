#pragma once
#include "tool.h"
#include <string>

namespace opencodecpp {

class WebSearchTool : public Tool {
public:
    std::string name() const override { return "web_search"; }
    std::string description() const override {
        return "Search the web for information. Returns a list of search results with titles, snippets, and URLs.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;

private:
    nlohmann::json searchSerpAPI(const std::string& query, const std::string& apiKey);
    nlohmann::json searchDuckDuckGo(const std::string& query);
    static std::string htmlDecode(const std::string& input);
};

} // namespace opencodecpp
