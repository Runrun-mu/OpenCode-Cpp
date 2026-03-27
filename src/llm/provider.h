#pragma once
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace opencodecpp {

struct Message {
    std::string role; // "user", "assistant", "system", "tool"
    nlohmann::json content; // string or array of content blocks
    nlohmann::json tool_calls; // for assistant messages with tool calls
    std::string tool_call_id; // for tool result messages
    std::string tool_use_id; // for Anthropic tool_result
    int input_tokens = 0;
    int output_tokens = 0;
};

struct ToolDef {
    std::string name;
    std::string description;
    nlohmann::json schema; // JSON Schema for parameters
};

struct ToolCall {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct LLMResponse {
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::string stop_reason; // "end_turn", "tool_use", "stop", etc.
    int input_tokens = 0;
    int output_tokens = 0;
    std::string error;
};

using TokenCallback = std::function<void(const std::string& token)>;
using ToolCallCallback = std::function<void(const ToolCall& call)>;

class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    virtual LLMResponse sendMessage(
        const std::vector<Message>& messages,
        const std::vector<ToolDef>& tools = {},
        const std::string& system_prompt = ""
    ) = 0;

    virtual LLMResponse streamMessage(
        const std::vector<Message>& messages,
        const std::vector<ToolDef>& tools,
        const std::string& system_prompt,
        TokenCallback onToken,
        ToolCallCallback onToolCall = nullptr,
        std::function<bool()> cancelCheck = nullptr
    ) = 0;

    virtual std::string name() const = 0;
};

} // namespace opencodecpp
