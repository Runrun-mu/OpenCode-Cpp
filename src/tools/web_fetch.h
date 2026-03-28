#pragma once
#include "tool.h"
#include <string>

namespace opencodecpp {

class WebFetchTool : public Tool {
public:
    std::string name() const override { return "web_fetch"; }
    std::string description() const override {
        return "Fetch content from a URL. HTML tags are stripped, returning plain text truncated to 10,000 characters.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;

    // Public static helpers for testing
    static std::string stripHtml(const std::string& html);
    static std::string truncateText(const std::string& text, size_t maxLen = 10000);

    static constexpr size_t MAX_TEXT_LENGTH = 10000;
};

} // namespace opencodecpp
