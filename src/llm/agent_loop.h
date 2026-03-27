#pragma once
#include "provider.h"
#include "../tools/tool.h"
#include "../session/session.h"
#include <map>
#include <memory>
#include <functional>

namespace opencodecpp {

struct AgentCallbacks {
    TokenCallback onToken;
    std::function<void(const std::string& toolName, const std::string& status)> onToolStatus;
    std::function<bool()> cancelCheck;
};

class AgentLoop {
public:
    AgentLoop(std::shared_ptr<LLMProvider> provider, SessionManager& session);

    void registerTool(std::shared_ptr<Tool> tool);

    // Run the agent loop: send user message, handle tool calls, return final text
    LLMResponse run(
        const std::string& userMessage,
        const std::string& systemPrompt,
        AgentCallbacks callbacks = {},
        int maxRounds = 20
    );

    int totalInputTokens() const { return totalInputTokens_; }
    int totalOutputTokens() const { return totalOutputTokens_; }

private:
    std::shared_ptr<LLMProvider> provider_;
    SessionManager& session_;
    std::map<std::string, std::shared_ptr<Tool>> tools_;
    std::vector<ToolDef> toolDefs_;
    int totalInputTokens_ = 0;
    int totalOutputTokens_ = 0;
};

} // namespace opencodecpp
