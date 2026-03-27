#include "anthropic.h"
#include "streaming.h"

namespace opencodecpp {

AnthropicProvider::AnthropicProvider(const std::string& apiKey, const std::string& model)
    : apiKey_(apiKey), model_(model) {}

std::map<std::string, std::string> AnthropicProvider::getHeaders() const {
    return {
        {"Content-Type", "application/json"},
        {"x-api-key", apiKey_},
        {"anthropic-version", "2023-06-01"}
    };
}

nlohmann::json AnthropicProvider::buildRequest(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt,
    bool stream
) const {
    nlohmann::json req;
    req["model"] = model_;
    req["max_tokens"] = 4096;
    if (stream) req["stream"] = true;

    if (!system_prompt.empty()) {
        req["system"] = system_prompt;
    }

    // Build messages array
    nlohmann::json msgs = nlohmann::json::array();
    for (auto& msg : messages) {
        nlohmann::json m;
        m["role"] = msg.role;

        if (msg.role == "tool") {
            // Convert to Anthropic tool_result format within user message
            // Anthropic expects tool_result as content blocks in "user" role
            nlohmann::json block;
            block["type"] = "tool_result";
            block["tool_use_id"] = msg.tool_use_id;
            if (msg.content.is_string()) {
                block["content"] = msg.content.get<std::string>();
            } else {
                block["content"] = msg.content.dump();
            }
            m["role"] = "user";
            m["content"] = nlohmann::json::array({block});
        } else if (msg.content.is_array()) {
            m["content"] = msg.content;
        } else if (msg.content.is_string()) {
            m["content"] = msg.content.get<std::string>();
        } else {
            m["content"] = msg.content.dump();
        }

        msgs.push_back(m);
    }
    req["messages"] = msgs;

    // Build tools array
    if (!tools.empty()) {
        nlohmann::json toolsArr = nlohmann::json::array();
        for (auto& t : tools) {
            nlohmann::json td;
            td["name"] = t.name;
            td["description"] = t.description;
            td["input_schema"] = t.schema;
            toolsArr.push_back(td);
        }
        req["tools"] = toolsArr;
    }

    return req;
}

LLMResponse AnthropicProvider::parseResponse(const nlohmann::json& j) const {
    LLMResponse resp;

    if (j.contains("error")) {
        resp.error = j["error"].value("message", j["error"].dump());
        return resp;
    }

    if (j.contains("usage")) {
        resp.input_tokens = j["usage"].value("input_tokens", 0);
        resp.output_tokens = j["usage"].value("output_tokens", 0);
    }

    resp.stop_reason = j.value("stop_reason", "");

    if (j.contains("content") && j["content"].is_array()) {
        for (auto& block : j["content"]) {
            std::string type = block.value("type", "");
            if (type == "text") {
                if (!resp.content.empty()) resp.content += "\n";
                resp.content += block.value("text", "");
            } else if (type == "tool_use") {
                ToolCall tc;
                tc.id = block.value("id", "");
                tc.name = block.value("name", "");
                tc.arguments = block.value("input", nlohmann::json::object());
                resp.tool_calls.push_back(tc);
            }
        }
    }

    return resp;
}

LLMResponse AnthropicProvider::sendMessage(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt
) {
    auto req = buildRequest(messages, tools, system_prompt, false);
    auto headers = getHeaders();

    auto httpResp = http_.post(API_URL, req.dump(), headers);

    if (!httpResp.error.empty()) {
        LLMResponse resp;
        resp.error = "HTTP error: " + httpResp.error;
        return resp;
    }

    try {
        auto j = nlohmann::json::parse(httpResp.body);
        return parseResponse(j);
    } catch (const std::exception& e) {
        LLMResponse resp;
        resp.error = std::string("JSON parse error: ") + e.what();
        return resp;
    }
}

LLMResponse AnthropicProvider::streamMessage(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt,
    TokenCallback onToken,
    ToolCallCallback onToolCall,
    std::function<bool()> cancelCheck
) {
    auto req = buildRequest(messages, tools, system_prompt, true);
    auto headers = getHeaders();

    LLMResponse result;
    std::string currentToolId;
    std::string currentToolName;
    std::string currentToolArgs;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
            std::string type = data.value("type", "");

            if (type == "message_start") {
                if (data.contains("message") && data["message"].contains("usage")) {
                    result.input_tokens = data["message"]["usage"].value("input_tokens", 0);
                }
            } else if (type == "content_block_start") {
                if (data.contains("content_block")) {
                    auto& block = data["content_block"];
                    if (block.value("type", "") == "tool_use") {
                        currentToolId = block.value("id", "");
                        currentToolName = block.value("name", "");
                        currentToolArgs = "";
                    }
                }
            } else if (type == "content_block_delta") {
                if (data.contains("delta")) {
                    auto& delta = data["delta"];
                    std::string deltaType = delta.value("type", "");
                    if (deltaType == "text_delta") {
                        std::string text = delta.value("text", "");
                        result.content += text;
                        if (onToken) onToken(text);
                    } else if (deltaType == "input_json_delta") {
                        currentToolArgs += delta.value("partial_json", "");
                    }
                }
            } else if (type == "content_block_stop") {
                if (!currentToolId.empty()) {
                    ToolCall tc;
                    tc.id = currentToolId;
                    tc.name = currentToolName;
                    try {
                        tc.arguments = nlohmann::json::parse(currentToolArgs);
                    } catch (...) {
                        tc.arguments = nlohmann::json::object();
                    }
                    result.tool_calls.push_back(tc);
                    if (onToolCall) onToolCall(tc);
                    currentToolId.clear();
                    currentToolName.clear();
                    currentToolArgs.clear();
                }
            } else if (type == "message_delta") {
                if (data.contains("delta")) {
                    result.stop_reason = data["delta"].value("stop_reason", "");
                }
                if (data.contains("usage")) {
                    result.output_tokens = data["usage"].value("output_tokens", 0);
                }
            }
        },
        [&]() {
            // Stream done
        }
    );

    http_.postStream(API_URL, req.dump(), headers,
        [&](const std::string& chunk) {
            parser.feed(chunk);
        },
        cancelCheck
    );

    return result;
}

} // namespace opencodecpp
