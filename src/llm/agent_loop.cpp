#include "agent_loop.h"

namespace opencodecpp {

AgentLoop::AgentLoop(std::shared_ptr<LLMProvider> provider, SessionManager& session)
    : provider_(provider), session_(session) {}

void AgentLoop::registerTool(std::shared_ptr<Tool> tool) {
    ToolDef def;
    def.name = tool->name();
    def.description = tool->description();
    def.schema = tool->schema();
    toolDefs_.push_back(def);
    tools_[tool->name()] = tool;
}

LLMResponse AgentLoop::run(
    const std::string& userMessage,
    const std::string& systemPrompt,
    AgentCallbacks callbacks,
    int maxRounds
) {
    // Add user message to history
    Message userMsg;
    userMsg.role = "user";
    userMsg.content = userMessage;
    session_.addMessage(userMsg, session_.currentSessionId());

    std::vector<Message> messages = session_.getHistory();

    LLMResponse finalResponse;

    for (int round = 0; round < maxRounds; round++) {
        if (callbacks.cancelCheck && callbacks.cancelCheck()) {
            finalResponse.content = "[Generation cancelled]";
            break;
        }

        LLMResponse resp;
        if (callbacks.onToken) {
            resp = provider_->streamMessage(
                messages, toolDefs_, systemPrompt,
                callbacks.onToken,
                [&](const ToolCall& tc) {
                    if (callbacks.onToolStatus) {
                        callbacks.onToolStatus(tc.name, "detected");
                    }
                },
                callbacks.cancelCheck
            );
        } else {
            resp = provider_->sendMessage(messages, toolDefs_, systemPrompt);
        }

        totalInputTokens_ += resp.input_tokens;
        totalOutputTokens_ += resp.output_tokens;

        if (!resp.error.empty()) {
            finalResponse.error = resp.error;
            break;
        }

        // Add assistant message to history
        Message assistantMsg;
        assistantMsg.role = "assistant";
        if (!resp.tool_calls.empty()) {
            // Build content blocks for Anthropic format
            nlohmann::json contentBlocks = nlohmann::json::array();
            if (!resp.content.empty()) {
                contentBlocks.push_back({{"type", "text"}, {"text", resp.content}});
            }
            for (auto& tc : resp.tool_calls) {
                contentBlocks.push_back({
                    {"type", "tool_use"},
                    {"id", tc.id},
                    {"name", tc.name},
                    {"input", tc.arguments}
                });
            }
            assistantMsg.content = contentBlocks;

            // Also store tool_calls for OpenAI format
            nlohmann::json tcArr = nlohmann::json::array();
            for (auto& tc : resp.tool_calls) {
                tcArr.push_back({
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", {{"name", tc.name}, {"arguments", tc.arguments.dump()}}}
                });
            }
            assistantMsg.tool_calls = tcArr;
        } else {
            assistantMsg.content = resp.content;
        }
        session_.addMessage(assistantMsg, session_.currentSessionId(), resp.output_tokens);
        messages.push_back(assistantMsg);

        // Check stop condition
        if (resp.tool_calls.empty()) {
            // No tool calls - we're done
            finalResponse = resp;
            break;
        }

        // Execute tool calls
        for (auto& tc : resp.tool_calls) {
            if (callbacks.onToolStatus) {
                callbacks.onToolStatus(tc.name, "running");
            }

            nlohmann::json result;
            auto it = tools_.find(tc.name);
            if (it != tools_.end()) {
                result = it->second->execute(tc.arguments);
            } else {
                result = {{"error", "Unknown tool: " + tc.name}};
            }

            if (callbacks.onToolStatus) {
                callbacks.onToolStatus(tc.name, "done");
            }

            // Add tool result message
            Message toolMsg;
            toolMsg.role = "tool";
            toolMsg.content = result.dump();
            toolMsg.tool_call_id = tc.id;    // OpenAI format
            toolMsg.tool_use_id = tc.id;     // Anthropic format
            session_.addMessage(toolMsg, session_.currentSessionId());
            messages.push_back(toolMsg);
        }

        finalResponse = resp;
    }

    return finalResponse;
}

std::string AgentLoop::compact(const std::string& systemPrompt) {
    auto history = session_.getHistory();
    if (history.empty()) return "";

    // Build conversation text
    std::string convText;
    for (auto& msg : history) {
        convText += msg.role + ": ";
        if (msg.content.is_string()) convText += msg.content.get<std::string>();
        else convText += msg.content.dump();
        convText += "\n";
    }

    std::string compactPrompt =
        "Summarize the following conversation. Include: current progress, "
        "key decisions, remaining tasks, critical file paths. "
        "Keep under 2000 tokens. Output only the summary.\n\n" + convText;

    Message compactMsg;
    compactMsg.role = "user";
    compactMsg.content = compactPrompt;

    auto resp = provider_->sendMessage({compactMsg}, {}, systemPrompt);

    if (!resp.error.empty()) return "Error: " + resp.error;

    Message summaryMsg;
    summaryMsg.role = "user";
    summaryMsg.content = "[Conversation Summary]\n" + resp.content;
    session_.clearAndReplace(summaryMsg);

    totalInputTokens_ += resp.input_tokens;
    totalOutputTokens_ += resp.output_tokens;

    return resp.content;
}

} // namespace opencodecpp
