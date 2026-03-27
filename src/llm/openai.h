#pragma once
#include "provider.h"
#include "../utils/http.h"

namespace opencodecpp {

class OpenAIProvider : public LLMProvider {
public:
    OpenAIProvider(const std::string& apiKey, const std::string& model = "gpt-4o",
                   const std::string& baseUrl = "https://api.openai.com");

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

    std::string name() const override { return "openai"; }

    // Public for testing
    nlohmann::json buildRequest(
        const std::vector<Message>& messages,
        const std::vector<ToolDef>& tools,
        const std::string& system_prompt,
        bool stream = false
    ) const;

    LLMResponse parseResponse(const nlohmann::json& response) const;
    std::map<std::string, std::string> getHeaders() const;

private:
    std::string apiKey_;
    std::string model_;
    std::string baseUrl_;
    HttpClient http_;
};

} // namespace opencodecpp
