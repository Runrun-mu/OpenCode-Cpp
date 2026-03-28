#include "subagent.h"
#include "../llm/agent_loop.h"
#include "../session/session.h"
#include "../config/config.h"
#include <thread>

namespace opencodecpp {

nlohmann::json SubagentTool::schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"prompt", {
                {"type", "string"},
                {"description", "The task for the sub-agent to perform"}
            }},
            {"tools", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "List of tool names the sub-agent should have access to"}
            }}
        }},
        {"required", nlohmann::json::array({"prompt"})}
    };
}

nlohmann::json SubagentTool::execute(const nlohmann::json& params) {
    if (!params.contains("prompt") || !params["prompt"].is_string()) {
        return {{"error", "Missing required parameter: prompt"}};
    }

    std::string prompt = params["prompt"].get<std::string>();

    // AC-18: Check max concurrent subagents
    if (activeCount().load() >= MAX_CONCURRENT) {
        return {{"error", "Maximum concurrent subagents (" + std::to_string(MAX_CONCURRENT) + ") exceeded"}};
    }

    // AC-19: Check max recursion depth
    if (currentDepth_ >= MAX_DEPTH) {
        return {{"error", "Maximum subagent recursion depth (" + std::to_string(MAX_DEPTH) + ") exceeded"}};
    }

    if (!provider_) {
        return {{"error", "No LLM provider configured for subagent"}};
    }

    // Get requested tools
    std::vector<std::string> requestedTools;
    if (params.contains("tools") && params["tools"].is_array()) {
        for (auto& t : params["tools"]) {
            if (t.is_string()) {
                requestedTools.push_back(t.get<std::string>());
            }
        }
    }

    // AC-17: Create a new AgentLoop with independent message history
    activeCount()++;

    try {
        // Create independent session for the subagent
        SessionManager subSession;
        subSession.initialize(Config::dbFilePath());
        subSession.createSession();

        AgentLoop subAgent(provider_, subSession);

        // Register only requested tools (or all if none specified)
        for (auto& [toolName, toolPtr] : availableTools_) {
            if (requestedTools.empty() ||
                std::find(requestedTools.begin(), requestedTools.end(), toolName) != requestedTools.end()) {
                // Don't register subagent tool itself to prevent unbounded recursion,
                // or register with increased depth
                if (toolName == "subagent") {
                    auto subagentTool = std::make_shared<SubagentTool>();
                    subagentTool->setProvider(provider_);
                    subagentTool->setAvailableTools(availableTools_);
                    subagentTool->setDepth(currentDepth_ + 1);
                    subAgent.registerTool(subagentTool);
                } else {
                    subAgent.registerTool(toolPtr);
                }
            }
        }

        std::string systemPrompt =
            "You are a sub-agent AI assistant. Complete the given task thoroughly and return a clear result.";

        auto resp = subAgent.run(prompt, systemPrompt, {}, 10);

        activeCount()--;

        if (!resp.error.empty()) {
            return {{"error", resp.error}};
        }

        return {
            {"result", resp.content},
            {"input_tokens", resp.input_tokens},
            {"output_tokens", resp.output_tokens}
        };
    } catch (const std::exception& e) {
        activeCount()--;
        return {{"error", std::string("Subagent error: ") + e.what()}};
    }
}

} // namespace opencodecpp
