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

    // Codex mode: use ChatGPT backend API with Responses format
    void setCodexMode(bool enabled, const std::string& accountId = "");
    bool isCodexMode() const { return codexMode_; }

    // Get the request URL based on mode
    std::string getRequestUrl() const;

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
    // Build request in Codex Responses API format
    nlohmann::json buildCodexRequest(
        const std::vector<Message>& messages,
        const std::vector<ToolDef>& tools,
        const std::string& system_prompt,
        bool stream = false
    ) const;

    std::string apiKey_;
    std::string model_;
    std::string baseUrl_;
    bool codexMode_ = false;
    std::string accountId_;
    HttpClient http_;
};

} // namespace opencodecpp
