#include "agent_loop.h"
#include <future>
#include <mutex>

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

// AC-12, AC-14: Add steer message to queue
void AgentLoop::addSteer(const std::string& steerText) {
    std::lock_guard<std::mutex> lock(steerMutex_);
    steerQueue_.push(steerText);
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

        // AC-14: Check steer queue at the beginning of each iteration
        {
            std::lock_guard<std::mutex> lock(steerMutex_);
            while (!steerQueue_.empty()) {
                std::string steerText = steerQueue_.front();
                steerQueue_.pop();
                Message steerMsg;
                steerMsg.role = "user";
                steerMsg.content = "[Steer] " + steerText;
                session_.addMessage(steerMsg, session_.currentSessionId());
                messages.push_back(steerMsg);
            }
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

        // Execute tool calls in parallel using std::async (F-5: AC-16, AC-17, AC-18, AC-19)
        std::vector<std::future<std::pair<ToolCall, nlohmann::json>>> futures;
        std::mutex callbackMutex;

        for (auto& tc : resp.tool_calls) {
            futures.push_back(std::async(std::launch::async, [&, tc]() {
                {
                    std::lock_guard<std::mutex> lock(callbackMutex);
                    if (callbacks.onToolStatus) {
                        callbacks.onToolStatus(tc.name, "running");
                    }
                }

                nlohmann::json result;
                auto it = tools_.find(tc.name);
                if (it != tools_.end()) {
                    result = it->second->execute(tc.arguments);
                } else {
                    result = {{"error", "Unknown tool: " + tc.name}};
                }

                {
                    std::lock_guard<std::mutex> lock(callbackMutex);
                    if (callbacks.onToolStatus) {
                        callbacks.onToolStatus(tc.name, "done");
                    }
                }

                return std::make_pair(tc, result);
            }));
        }

        // Collect all results before adding to message history
        for (auto& f : futures) {
            auto [tc, result] = f.get();
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

LLMResponse AgentLoop::runPlan(
    const std::string& userMessage,
    const std::string& systemPrompt,
    const std::vector<std::string>& allowedTools,
    AgentCallbacks callbacks,
    int maxRounds
) {
    // Save original tools
    auto originalToolDefs = toolDefs_;
    auto originalTools = tools_;

    // Filter to only allowed tools
    std::vector<ToolDef> filteredDefs;
    std::map<std::string, std::shared_ptr<Tool>> filteredTools;

    for (const auto& toolName : allowedTools) {
        auto it = originalTools.find(toolName);
        if (it != originalTools.end()) {
            filteredTools[toolName] = it->second;
            for (const auto& def : originalToolDefs) {
                if (def.name == toolName) {
                    filteredDefs.push_back(def);
                    break;
                }
            }
        }
    }

    toolDefs_ = filteredDefs;
    tools_ = filteredTools;

    // Delegate to run() for actual execution
    auto result = run(userMessage, systemPrompt, callbacks, maxRounds);

    // Restore original tools
    toolDefs_ = originalToolDefs;
    tools_ = originalTools;

    return result;
}

CompactResult AgentLoop::compact(const std::string& systemPrompt) {
    CompactResult result;
    auto history = session_.getHistory();
    result.originalMessageCount = static_cast<int>(history.size());

    if (history.empty()) {
        result.error = "No messages to compact";
        return result;
    }

    // Estimate tokens before compaction
    double tokensBefore = session_.estimateTokens();

    // Three-layer architecture:
    // Layer 1: System prompt messages (preserved, never sent for summarization)
    // Layer 2: Recent ~20000 tokens of messages (kept intact)
    // Layer 3: Older messages (sent to LLM for summarization, replaced by summary)

    std::vector<Message> systemMessages;
    std::vector<Message> nonSystemMessages;

    for (auto& msg : history) {
        if (msg.role == "system") {
            systemMessages.push_back(msg);
        } else {
            nonSystemMessages.push_back(msg);
        }
    }

    // Get recent messages (~20000 tokens worth)
    const int recentTokenLimit = 20000;
    std::vector<Message> recentMessages;
    std::vector<Message> olderMessages;

    double accumulatedTokens = 0.0;
    int splitIndex = static_cast<int>(nonSystemMessages.size());

    // Iterate from newest to oldest to find recent messages
    for (int i = static_cast<int>(nonSystemMessages.size()) - 1; i >= 0; i--) {
        size_t chars = 0;
        if (nonSystemMessages[i].content.is_string()) {
            chars = nonSystemMessages[i].content.get<std::string>().size();
        } else {
            chars = nonSystemMessages[i].content.dump().size();
        }
        double msgTokens = (static_cast<double>(chars) / 4.0) * 1.2;

        if (accumulatedTokens + msgTokens > recentTokenLimit && !recentMessages.empty()) {
            splitIndex = i + 1;
            break;
        }
        recentMessages.insert(recentMessages.begin(), nonSystemMessages[i]);
        accumulatedTokens += msgTokens;
        if (i == 0) splitIndex = 0;
    }

    // Fix split boundary: ensure recentMessages doesn't start with orphan "tool" messages
    // (tool messages must follow an assistant message with tool_calls)
    while (splitIndex < static_cast<int>(nonSystemMessages.size()) &&
           nonSystemMessages[splitIndex].role == "tool") {
        // Pull this tool msg (and its preceding assistant) into older
        splitIndex++;
        if (!recentMessages.empty() && recentMessages.front().role == "tool") {
            recentMessages.erase(recentMessages.begin());
        }
    }
    // Also ensure we don't split in the middle of an assistant+tool sequence
    // If splitIndex-1 is an assistant with tool_calls, pull it and all following tools into recent
    if (splitIndex > 0 && splitIndex < static_cast<int>(nonSystemMessages.size())) {
        auto& prev = nonSystemMessages[splitIndex - 1];
        if (prev.role == "assistant" && !prev.tool_calls.is_null() && !prev.tool_calls.empty()) {
            // The assistant's tool results are in recentMessages but the assistant is in older
            // Move the assistant into recent
            splitIndex--;
            recentMessages.insert(recentMessages.begin(), nonSystemMessages[splitIndex]);
        }
    }

    // Older messages are everything before the split
    for (int i = 0; i < splitIndex; i++) {
        olderMessages.push_back(nonSystemMessages[i]);
    }

    // If there are no older messages to summarize, nothing to compact
    if (olderMessages.empty()) {
        result.error = "Not enough messages to compact";
        return result;
    }

    // Build conversation text from older messages for summarization
    std::string convText;
    for (auto& msg : olderMessages) {
        convText += msg.role + ": ";
        if (msg.content.is_string()) convText += msg.content.get<std::string>();
        else convText += msg.content.dump();
        convText += "\n";
    }

    // Send the Codex CONTEXT CHECKPOINT COMPACTION prompt (AC-7)
    std::string compactPrompt =
        "You are performing a CONTEXT CHECKPOINT COMPACTION. "
        "Create a handoff summary for another LLM that will resume the task. "
        "Include: 1) Current progress and key decisions made, "
        "2) Important context, constraints, or user preferences, "
        "3) What remains to be done (clear next steps), "
        "4) Any critical data, file paths, examples, or references needed to continue. "
        "Be concise, structured, and focused on helping the next LLM seamlessly continue the work.\n\n"
        "Conversation to summarize:\n" + convText;

    Message compactMsg;
    compactMsg.role = "user";
    compactMsg.content = compactPrompt;

    auto resp = provider_->sendMessage({compactMsg}, {}, systemPrompt);

    if (!resp.error.empty()) {
        result.error = "Error: " + resp.error;
        return result;
    }

    totalInputTokens_ += resp.input_tokens;
    totalOutputTokens_ += resp.output_tokens;

    // Build the summary message with the required prefix (AC-9)
    std::string summaryPrefix =
        "Another language model started to solve this problem and produced a summary of its thinking process. "
        "You also have access to the state of the tools that were used. "
        "Use this to build on the work that has already been done and avoid duplicating work. "
        "Here is the summary:";

    Message summaryMsg;
    summaryMsg.role = "user";
    summaryMsg.content = summaryPrefix + "\n\n" + resp.content;

    // Rebuild history with three layers:
    // Layer 1: system messages (preserved)
    // Layer 3: compressed summary (replacing older messages)
    // Layer 2: recent messages (intact)
    std::vector<Message> newHistory;

    // Add system messages first
    for (auto& msg : systemMessages) {
        newHistory.push_back(msg);
    }

    // Add summary message
    newHistory.push_back(summaryMsg);

    // Add recent messages (intact, but skip orphan tool messages)
    // An orphan tool msg is one where the preceding msg is not assistant with tool_calls
    for (size_t i = 0; i < recentMessages.size(); i++) {
        if (recentMessages[i].role == "tool") {
            // Check if preceded by assistant with tool_calls
            bool hasParent = false;
            if (i > 0 && recentMessages[i-1].role == "assistant" &&
                !recentMessages[i-1].tool_calls.is_null() && !recentMessages[i-1].tool_calls.empty()) {
                hasParent = true;
            }
            // Also check in newHistory (summary msg is user role, won't match)
            if (!hasParent && !newHistory.empty()) {
                auto& last = newHistory.back();
                if (last.role == "assistant" && !last.tool_calls.is_null() && !last.tool_calls.empty()) {
                    hasParent = true;
                }
            }
            if (!hasParent) {
                // Convert orphan tool msg to user msg to preserve info
                Message converted;
                converted.role = "user";
                if (recentMessages[i].content.is_string()) {
                    converted.content = "[Previous tool result]: " + recentMessages[i].content.get<std::string>();
                } else {
                    converted.content = "[Previous tool result]: " + recentMessages[i].content.dump();
                }
                newHistory.push_back(converted);
                continue;
            }
        }
        newHistory.push_back(recentMessages[i]);
    }

    // Replace session history
    session_.clearAndReplace(newHistory[0]);
    for (size_t i = 1; i < newHistory.size(); i++) {
        session_.addMessage(newHistory[i], session_.currentSessionId());
    }

    // But clearAndReplace only keeps one message, so we need to rebuild properly
    // Actually, let's use clearAndReplace for the first, then addMessage for rest
    // Wait - clearAndReplace clears and adds one. But we actually need to clear and
    // replace with multiple messages. Let's just manipulate directly.
    // The clearAndReplace clears history_ and pushes one msg.
    // Then addMessage pushes to history_ too. So this pattern works.

    // Calculate stats
    double tokensAfter = session_.estimateTokens();
    result.summary = resp.content;
    result.remainingMessageCount = static_cast<int>(newHistory.size());
    result.tokensSaved = tokensBefore - tokensAfter;
    result.success = true;

    // Build status message (AC-10)
    result.statusMessage = "Context compacted: " +
        std::to_string(result.originalMessageCount) + " messages -> summary + " +
        std::to_string(static_cast<int>(recentMessages.size())) + " recent messages. " +
        "Tokens saved: " + std::to_string(static_cast<int>(result.tokensSaved));

    return result;
}

} // namespace opencodecpp
