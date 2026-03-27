#include "openai.h"
#include "streaming.h"

namespace opencodecpp {

OpenAIProvider::OpenAIProvider(const std::string& apiKey, const std::string& model, const std::string& baseUrl)
    : apiKey_(apiKey), model_(model), baseUrl_(baseUrl) {}

std::map<std::string, std::string> OpenAIProvider::getHeaders() const {
    return {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + apiKey_}
    };
}

nlohmann::json OpenAIProvider::buildRequest(
    const std::vector<Message>& messages,
    const std::vector<ToolDef>& tools,
    const std::string& system_prompt,
    bool stream
) const {
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
    std::string url = baseUrl_ + "/v1/chat/completions";

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
    std::string url = baseUrl_ + "/v1/chat/completions";

    LLMResponse result;

    // Map of tool call index -> partial tool call
    std::map<int, ToolCall> partialToolCalls;
    std::map<int, std::string> argStrings;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
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
            // Stream done - finalize tool calls
            for (auto& [idx, tc] : partialToolCalls) {
                result.tool_calls.push_back(tc);
                if (onToolCall) onToolCall(tc);
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
