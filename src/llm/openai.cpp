#include "openai.h"
#include "streaming.h"

namespace opencodecpp {

OpenAIProvider::OpenAIProvider(const std::string& apiKey, const std::string& model, const std::string& baseUrl)
    : apiKey_(apiKey), model_(model), baseUrl_(baseUrl) {}

void OpenAIProvider::setCodexMode(bool enabled, const std::string& accountId) {
    codexMode_ = enabled;
    accountId_ = accountId;
}

std::string OpenAIProvider::getRequestUrl() const {
    if (codexMode_) {
        return baseUrl_ + "/codex/responses";
    }
    return baseUrl_ + "/v1/chat/completions";
}

std::map<std::string, std::string> OpenAIProvider::getHeaders() const {
    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + apiKey_}
    };

    if (codexMode_) {
        headers["chatgpt-account-id"] = accountId_;
        headers["OpenAI-Beta"] = "responses=experimental";
        headers["originator"] = "codex_cli_rs";
        headers["accept"] = "text/event-stream";
    }

    return headers;
}

nlohmann::json OpenAIProvider::buildCodexRequest(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt,
    bool stream
) const {
    nlohmann::json req;
    req["model"] = model_;
    req["store"] = false;
    req["stream"] = stream;

    if (!system_prompt.empty()) {
        req["instructions"] = system_prompt;
    }

    // Convert messages to Codex Responses API input format
    // Responses API only accepts: user, assistant, system, developer
    // Tool results must use type: "function_call_output"
    // Assistant tool calls must use type: "function_call"
    nlohmann::json input = nlohmann::json::array();
    for (auto& msg : messages) {
        if (msg.role == "tool") {
            // Tool result → function_call_output
            nlohmann::json item;
            item["type"] = "function_call_output";
            item["call_id"] = msg.tool_call_id.empty() ? msg.tool_use_id : msg.tool_call_id;
            if (msg.content.is_string()) {
                item["output"] = msg.content.get<std::string>();
            } else {
                item["output"] = msg.content.dump();
            }
            input.push_back(item);
        } else if (msg.role == "assistant" && !msg.tool_calls.is_null() && !msg.tool_calls.empty()) {
            // Assistant message with tool calls
            // First add text content if any
            if (msg.content.is_string() && !msg.content.get<std::string>().empty()) {
                nlohmann::json textItem;
                textItem["role"] = "assistant";
                textItem["content"] = msg.content.get<std::string>();
                input.push_back(textItem);
            }
            // Then add each tool call as function_call type
            for (auto& tc : msg.tool_calls) {
                nlohmann::json callItem;
                callItem["type"] = "function_call";
                callItem["call_id"] = tc.value("id", "");
                if (tc.contains("function")) {
                    callItem["name"] = tc["function"].value("name", "");
                    callItem["arguments"] = tc["function"].value("arguments", "{}");
                }
                input.push_back(callItem);
            }
        } else {
            // Regular user/assistant/system message
            nlohmann::json item;
            item["role"] = msg.role;
            if (msg.content.is_string()) {
                item["content"] = msg.content.get<std::string>();
            } else {
                item["content"] = msg.content.dump();
            }
            input.push_back(item);
        }
    }
    req["input"] = input;

    // Add tools if present
    if (!tools.empty()) {
        nlohmann::json toolsArr = nlohmann::json::array();
        for (auto& t : tools) {
            nlohmann::json td;
            td["type"] = "function";
            td["name"] = t.name;
            td["description"] = t.description;
            td["parameters"] = t.schema;
            toolsArr.push_back(td);
        }
        req["tools"] = toolsArr;
    }

    return req;
}

nlohmann::json OpenAIProvider::buildRequest(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt,
    bool stream
) const {
    // Codex mode uses Responses API format
    if (codexMode_) {
        return buildCodexRequest(messages, tools, system_prompt, stream);
    }

    nlohmann::json req;
    req["model"] = model_;
    if (stream) {
        req["stream"] = true;
        req["stream_options"] = {{"include_usage", true}};
    }

    nlohmann::json msgs = nlohmann::json::array();

    if (!system_prompt.empty()) {
        nlohmann::json sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = system_prompt;
        msgs.push_back(sysMsg);
    }

    for (auto& msg : messages) {
        nlohmann::json m;
        m["role"] = msg.role;

        if (msg.role == "tool") {
            m["tool_call_id"] = msg.tool_call_id;
            if (msg.content.is_string()) {
                m["content"] = msg.content.get<std::string>();
            } else {
                m["content"] = msg.content.dump();
            }
        } else if (msg.role == "assistant" && !msg.tool_calls.is_null() && !msg.tool_calls.empty()) {
            if (msg.content.is_string()) {
                m["content"] = msg.content.get<std::string>();
            } else {
                m["content"] = "";
            }
            m["tool_calls"] = msg.tool_calls;
        } else {
            if (msg.content.is_string()) {
                m["content"] = msg.content.get<std::string>();
            } else {
                m["content"] = msg.content.dump();
            }
        }

        msgs.push_back(m);
    }
    req["messages"] = msgs;

    if (!tools.empty()) {
        nlohmann::json toolsArr = nlohmann::json::array();
        for (auto& t : tools) {
            nlohmann::json td;
            td["type"] = "function";
            td["function"]["name"] = t.name;
            td["function"]["description"] = t.description;
            td["function"]["parameters"] = t.schema;
            toolsArr.push_back(td);
        }
        req["tools"] = toolsArr;
    }

    return req;
}

LLMResponse OpenAIProvider::parseResponse(const nlohmann::json& j) const {
    LLMResponse resp;

    if (j.contains("error")) {
        resp.error = j["error"].value("message", j["error"].dump());
        return resp;
    }

    if (j.contains("usage")) {
        resp.input_tokens = j["usage"].value("prompt_tokens", 0);
        resp.output_tokens = j["usage"].value("completion_tokens", 0);
    }

    if (j.contains("choices") && !j["choices"].empty()) {
        auto& choice = j["choices"][0];
        resp.stop_reason = choice.value("finish_reason", "");

        if (choice.contains("message")) {
            auto& msg = choice["message"];
            if (msg.contains("content") && !msg["content"].is_null()) {
                resp.content = msg["content"].get<std::string>();
            }

            if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                for (auto& tc : msg["tool_calls"]) {
                    ToolCall call;
                    call.id = tc.value("id", "");
                    if (tc.contains("function")) {
                        call.name = tc["function"].value("name", "");
                        std::string args = tc["function"].value("arguments", "{}");
                        try {
                            call.arguments = nlohmann::json::parse(args);
                        } catch (...) {
                            call.arguments = nlohmann::json::object();
                        }
                    }
                    resp.tool_calls.push_back(call);
                }
            }
        }
    }

    return resp;
}

LLMResponse OpenAIProvider::sendMessage(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt
) {
    auto req = buildRequest(messages, tools, system_prompt, false);
    auto headers = getHeaders();
    std::string url = getRequestUrl();

    auto httpResp = http_.post(url, req.dump(), headers);

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

LLMResponse OpenAIProvider::streamMessage(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt,
    TokenCallback onToken,
    ToolCallCallback onToolCall,
    std::function<bool()> cancelCheck
) {
    auto req = buildRequest(messages, tools, system_prompt, true);
    auto headers = getHeaders();
    std::string url = getRequestUrl();

    LLMResponse result;

    // Map of tool call index -> partial tool call
    std::map<int, ToolCall> partialToolCalls;
    std::map<int, std::string> argStrings;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
            // Handle Codex Responses API SSE events (AC-14)
            if (codexMode_) {
                std::string type = data.value("type", "");
                if (type == "response.output_text.delta") {
                    std::string text = data.value("delta", "");
                    if (!text.empty()) {
                        result.content += text;
                        if (onToken) onToken(text);
                    }
                } else if (type == "response.done") {
                    result.stop_reason = "stop";
                    // Extract usage if available
                    if (data.contains("response") && data["response"].contains("usage")) {
                        auto& usage = data["response"]["usage"];
                        result.input_tokens = usage.value("input_tokens", 0);
                        result.output_tokens = usage.value("output_tokens", 0);
                    }
                } else if (type == "response.function_call_arguments.delta") {
                    // Handle function call streaming for codex mode
                    int idx = data.value("output_index", 0);
                    std::string argDelta = data.value("delta", "");
                    argStrings[idx] += argDelta;
                    if (data.contains("call_id")) {
                        partialToolCalls[idx].id = data["call_id"].get<std::string>();
                    }
                    if (data.contains("name")) {
                        partialToolCalls[idx].name = data["name"].get<std::string>();
                    }
                    try {
                        partialToolCalls[idx].arguments = nlohmann::json::parse(argStrings[idx]);
                    } catch (...) {}
                } else if (type == "response.function_call_arguments.done") {
                    int idx = data.value("output_index", 0);
                    if (partialToolCalls.count(idx)) {
                        result.tool_calls.push_back(partialToolCalls[idx]);
                        if (onToolCall) onToolCall(partialToolCalls[idx]);
                    }
                }
                return;
            }

            // Standard Chat Completions SSE handling
            if (!data.contains("choices") || data["choices"].empty()) {
                // Check for usage in stream
                if (data.contains("usage")) {
                    result.input_tokens = data["usage"].value("prompt_tokens", 0);
                    result.output_tokens = data["usage"].value("completion_tokens", 0);
                }
                return;
            }

            auto& choice = data["choices"][0];

            if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                result.stop_reason = choice["finish_reason"].get<std::string>();
            }

            if (choice.contains("delta")) {
                auto& delta = choice["delta"];

                // Text content
                if (delta.contains("content") && !delta["content"].is_null()) {
                    std::string text = delta["content"].get<std::string>();
                    result.content += text;
                    if (onToken) onToken(text);
                }

                // Tool calls
                if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                    for (auto& tc : delta["tool_calls"]) {
                        int idx = tc.value("index", 0);
                        if (tc.contains("id")) {
                            partialToolCalls[idx].id = tc["id"].get<std::string>();
                        }
                        if (tc.contains("function")) {
                            if (tc["function"].contains("name")) {
                                partialToolCalls[idx].name = tc["function"]["name"].get<std::string>();
                            }
                            if (tc["function"].contains("arguments")) {
                                partialToolCalls[idx].arguments = nlohmann::json::object(); // placeholder
                                // Accumulate arguments string
                                argStrings[idx] += tc["function"]["arguments"].get<std::string>();
                                try {
                                    partialToolCalls[idx].arguments = nlohmann::json::parse(argStrings[idx]);
                                } catch (...) {
                                    // Still accumulating
                                }
                            }
                        }
                    }
                }
            }
        },
        [&]() {
            // Stream done - finalize tool calls (for non-codex mode)
            if (!codexMode_) {
                for (auto& [idx, tc] : partialToolCalls) {
                    result.tool_calls.push_back(tc);
                    if (onToolCall) onToolCall(tc);
                }
            }
        }
    );

    std::string streamBody;

    auto httpResp = http_.postStream(url, req.dump(), headers,
        [&](const std::string& chunk) {
            streamBody += chunk;
            parser.feed(chunk);
        },
        cancelCheck
    );

    // Check for curl-level errors
    if (!httpResp.error.empty()) {
        result.error = "HTTP error: " + httpResp.error;
    }
    // Check for non-200 HTTP status codes
    else if (httpResp.status_code != 0 && httpResp.status_code != 200) {
        // Try to parse the response body as JSON error
        try {
            auto errJson = nlohmann::json::parse(streamBody);
            if (errJson.contains("error")) {
                result.error = "HTTP " + std::to_string(httpResp.status_code) + ": " +
                    errJson["error"].value("message", errJson["error"].dump());
            } else {
                result.error = "HTTP error " + std::to_string(httpResp.status_code);
            }
        } catch (...) {
            result.error = "HTTP error " + std::to_string(httpResp.status_code) + ": " + streamBody;
        }
    }

    return result;
}

} // namespace opencodecpp
