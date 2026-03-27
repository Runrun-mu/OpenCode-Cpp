#pragma once
#include "provider.h"
#include "../utils/http.h"

namespace opencodecpp {

class AnthropicProvider : public LLMProvider {
public:
    AnthropicProvider(const std::string& apiKey, const std::string& model = "claude-sonnet-4-20250514");

    LLMResponse sendMessage(
        const std::vector<Message>& messages,
        const std::vector<ToolDef>& tools = {},
        const std::string& system_prompt = ""
    ) override;

    LLMResponse streamMessage(
        const std::vector<Message>& messages,
        const std::vector<ToolDef>& tools,
        const std::string& system_prompt,
        TokenCallback onToken,
        ToolCallCallback onToolCall = nullptr,
        std::function<bool()> cancelCheck = nullptr
    ) override;

    std::string name() const override { return "anthropic"; }

    // Build request JSON (public for testing)
    nlohmann::json buildRequest(
        const std::vector<Message>& messages,
        const std::vector<ToolDef>& tools,
        const std::string& system_prompt,
        bool stream = false
    ) const;

    // Parse response JSON (public for testing)
    LLMResponse parseResponse(const nlohmann::json& response) const;

    std::map<std::string, std::string> getHeaders() const;

private:
    std::string apiKey_;
    std::string model_;
    HttpClient http_;
    static constexpr const char* API_URL = "https://api.anthropic.com/v1/messages";
};

} // namespace opencodecpp
