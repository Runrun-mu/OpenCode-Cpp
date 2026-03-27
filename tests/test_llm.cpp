#include <iostream>
#include <cstdlib>
#include "llm/provider.h"
#include "llm/anthropic.h"
#include "llm/openai.h"
#include "llm/streaming.h"

using namespace opencodecpp;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    tests_run++; \
    try { test_##name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED (unknown exception)\n"; }

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x)
#define ASSERT_FALSE(x) if (x) throw std::runtime_error(std::string("Assertion failed: !") + #x)
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b)

// === AC-13: Anthropic provider builds correct requests ===
void test_anthropic_build_request() {
    AnthropicProvider provider("test-key", "claude-sonnet-4-20250514");

    std::vector<Message> msgs;
    Message m;
    m.role = "user";
    m.content = "Hello";
    msgs.push_back(m);

    auto req = provider.buildRequest(msgs, {}, "You are helpful", false);

    ASSERT_EQ(req["model"].get<std::string>(), std::string("claude-sonnet-4-20250514"));
    ASSERT_EQ(req["system"].get<std::string>(), std::string("You are helpful"));
    ASSERT_TRUE(req.contains("messages"));
    ASSERT_TRUE(req["messages"].is_array());
    ASSERT_TRUE(req["messages"].size() == 1);
    ASSERT_EQ(req["messages"][0]["role"].get<std::string>(), std::string("user"));
}

void test_anthropic_build_request_with_tools() {
    AnthropicProvider provider("test-key");

    std::vector<Message> msgs;
    Message m;
    m.role = "user";
    m.content = "Run a command";
    msgs.push_back(m);

    std::vector<ToolDef> tools;
    ToolDef t;
    t.name = "bash";
    t.description = "Execute a shell command";
    t.schema = {{"type", "object"}, {"properties", {{"command", {{"type", "string"}}}}}};
    tools.push_back(t);

    auto req = provider.buildRequest(msgs, tools, "", false);
    ASSERT_TRUE(req.contains("tools"));
    ASSERT_TRUE(req["tools"].size() == 1);
    ASSERT_EQ(req["tools"][0]["name"].get<std::string>(), std::string("bash"));
    ASSERT_TRUE(req["tools"][0].contains("input_schema"));
}

void test_anthropic_build_stream_request() {
    AnthropicProvider provider("test-key");
    std::vector<Message> msgs;
    Message m; m.role = "user"; m.content = "Hello"; msgs.push_back(m);
    auto req = provider.buildRequest(msgs, {}, "", true);
    ASSERT_TRUE(req.contains("stream"));
    ASSERT_TRUE(req["stream"].get<bool>());
}

void test_anthropic_parse_response() {
    AnthropicProvider provider("test-key");

    nlohmann::json resp = {
        {"content", {{{"type", "text"}, {"text", "Hello there!"}}}},
        {"stop_reason", "end_turn"},
        {"usage", {{"input_tokens", 10}, {"output_tokens", 5}}}
    };

    auto result = provider.parseResponse(resp);
    ASSERT_EQ(result.content, std::string("Hello there!"));
    ASSERT_EQ(result.stop_reason, std::string("end_turn"));
    ASSERT_EQ(result.input_tokens, 10);
    ASSERT_EQ(result.output_tokens, 5);
    ASSERT_TRUE(result.error.empty());
}

void test_anthropic_parse_tool_use() {
    AnthropicProvider provider("test-key");

    nlohmann::json resp = {
        {"content", {
            {{"type", "text"}, {"text", "Let me check."}},
            {{"type", "tool_use"}, {"id", "toolu_123"}, {"name", "bash"}, {"input", {{"command", "ls"}}}}
        }},
        {"stop_reason", "tool_use"},
        {"usage", {{"input_tokens", 20}, {"output_tokens", 15}}}
    };

    auto result = provider.parseResponse(resp);
    ASSERT_EQ(result.content, std::string("Let me check."));
    ASSERT_TRUE(result.tool_calls.size() == 1);
    ASSERT_EQ(result.tool_calls[0].id, std::string("toolu_123"));
    ASSERT_EQ(result.tool_calls[0].name, std::string("bash"));
    ASSERT_EQ(result.tool_calls[0].arguments["command"].get<std::string>(), std::string("ls"));
}

void test_anthropic_parse_error() {
    AnthropicProvider provider("test-key");

    nlohmann::json resp = {
        {"error", {{"message", "Invalid API key"}}}
    };

    auto result = provider.parseResponse(resp);
    ASSERT_TRUE(result.error.find("Invalid API key") != std::string::npos);
}

void test_anthropic_headers() {
    AnthropicProvider provider("my-api-key");
    auto headers = provider.getHeaders();
    ASSERT_EQ(headers["x-api-key"], std::string("my-api-key"));
    ASSERT_TRUE(headers.find("anthropic-version") != headers.end());
    ASSERT_TRUE(headers.find("Content-Type") != headers.end());
}

void test_anthropic_name() {
    AnthropicProvider provider("key");
    ASSERT_EQ(provider.name(), std::string("anthropic"));
}

// === AC-14: OpenAI provider ===
void test_openai_build_request() {
    OpenAIProvider provider("test-key", "gpt-4o");

    std::vector<Message> msgs;
    Message m;
    m.role = "user";
    m.content = "Hello";
    msgs.push_back(m);

    auto req = provider.buildRequest(msgs, {}, "You are helpful", false);

    ASSERT_EQ(req["model"].get<std::string>(), std::string("gpt-4o"));
    ASSERT_TRUE(req["messages"].size() == 2); // system + user
    ASSERT_EQ(req["messages"][0]["role"].get<std::string>(), std::string("system"));
    ASSERT_EQ(req["messages"][1]["role"].get<std::string>(), std::string("user"));
}

void test_openai_build_request_with_tools() {
    OpenAIProvider provider("test-key");

    std::vector<Message> msgs;
    Message m; m.role = "user"; m.content = "Run a command"; msgs.push_back(m);

    std::vector<ToolDef> tools;
    ToolDef t;
    t.name = "bash";
    t.description = "Execute command";
    t.schema = {{"type", "object"}, {"properties", {{"command", {{"type", "string"}}}}}};
    tools.push_back(t);

    auto req = provider.buildRequest(msgs, tools, "", false);
    ASSERT_TRUE(req.contains("tools"));
    ASSERT_TRUE(req["tools"].size() == 1);
    ASSERT_EQ(req["tools"][0]["type"].get<std::string>(), std::string("function"));
    ASSERT_EQ(req["tools"][0]["function"]["name"].get<std::string>(), std::string("bash"));
}

void test_openai_parse_response() {
    OpenAIProvider provider("test-key");

    nlohmann::json resp = {
        {"choices", {{
            {"message", {{"role", "assistant"}, {"content", "Hi!"}}},
            {"finish_reason", "stop"}
        }}},
        {"usage", {{"prompt_tokens", 10}, {"completion_tokens", 3}}}
    };

    auto result = provider.parseResponse(resp);
    ASSERT_EQ(result.content, std::string("Hi!"));
    ASSERT_EQ(result.stop_reason, std::string("stop"));
    ASSERT_EQ(result.input_tokens, 10);
    ASSERT_EQ(result.output_tokens, 3);
}

void test_openai_parse_tool_call() {
    OpenAIProvider provider("test-key");

    nlohmann::json resp = {
        {"choices", {{
            {"message", {
                {"role", "assistant"},
                {"content", nullptr},
                {"tool_calls", {{
                    {"id", "call_123"},
                    {"type", "function"},
                    {"function", {{"name", "bash"}, {"arguments", "{\"command\":\"ls\"}"}}}
                }}}
            }},
            {"finish_reason", "tool_calls"}
        }}},
        {"usage", {{"prompt_tokens", 20}, {"completion_tokens", 10}}}
    };

    auto result = provider.parseResponse(resp);
    ASSERT_TRUE(result.tool_calls.size() == 1);
    ASSERT_EQ(result.tool_calls[0].id, std::string("call_123"));
    ASSERT_EQ(result.tool_calls[0].name, std::string("bash"));
}

void test_openai_headers() {
    OpenAIProvider provider("my-key");
    auto headers = provider.getHeaders();
    ASSERT_EQ(headers["Authorization"], std::string("Bearer my-key"));
}

void test_openai_name() {
    OpenAIProvider provider("key");
    ASSERT_EQ(provider.name(), std::string("openai"));
}

// AC-15: Custom base URL
void test_openai_custom_base_url() {
    OpenAIProvider provider("key", "gpt-4o", "https://custom.api.com");
    // Just verify construction doesn't crash
    ASSERT_EQ(provider.name(), std::string("openai"));
}

// === AC-16: SSE Parser ===
void test_sse_parser_text_data() {
    std::string received_event;
    nlohmann::json received_data;
    bool done = false;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
            received_event = event;
            received_data = data;
        },
        [&]() { done = true; }
    );

    parser.feed("data: {\"text\": \"hello\"}\n\n");
    ASSERT_EQ(received_data["text"].get<std::string>(), std::string("hello"));
}

void test_sse_parser_event_type() {
    std::string received_event;
    nlohmann::json received_data;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
            received_event = event;
            received_data = data;
        },
        nullptr
    );

    parser.feed("event: content_block_delta\ndata: {\"type\": \"content_block_delta\"}\n\n");
    ASSERT_EQ(received_event, std::string("content_block_delta"));
}

void test_sse_parser_done() {
    bool done = false;

    SSEParser parser(
        [](const std::string&, const nlohmann::json&) {},
        [&]() { done = true; }
    );

    parser.feed("data: [DONE]\n\n");
    ASSERT_TRUE(done);
    ASSERT_TRUE(parser.isDone());
}

// AC-16: message_stop termination
void test_sse_parser_anthropic_stop() {
    std::string lastEvent;

    SSEParser parser(
        [&](const std::string& event, const nlohmann::json& data) {
            lastEvent = data.value("type", "");
        },
        nullptr
    );

    parser.feed("event: message_stop\ndata: {\"type\": \"message_stop\"}\n\n");
    ASSERT_EQ(lastEvent, std::string("message_stop"));
}

void test_sse_parser_partial_lines() {
    nlohmann::json received_data;

    SSEParser parser(
        [&](const std::string&, const nlohmann::json& data) {
            received_data = data;
        },
        nullptr
    );

    // Feed partial data
    parser.feed("data: {\"val\":");
    ASSERT_TRUE(received_data.is_null());
    // Complete the line
    parser.feed(" 42}\n\n");
    ASSERT_EQ(received_data["val"].get<int>(), 42);
}

void test_sse_parser_reset() {
    SSEParser parser(nullptr, nullptr);
    parser.feed("data: [DONE]\n");
    ASSERT_TRUE(parser.isDone());
    parser.reset();
    ASSERT_FALSE(parser.isDone());
}

// AC-17: HTTP error messages
void test_anthropic_parse_401_error() {
    AnthropicProvider provider("bad-key");
    nlohmann::json resp = {
        {"error", {{"type", "authentication_error"}, {"message", "Invalid API key provided"}}}
    };
    auto result = provider.parseResponse(resp);
    ASSERT_TRUE(result.error.find("Invalid API key") != std::string::npos);
}

void test_openai_parse_error() {
    OpenAIProvider provider("bad-key");
    nlohmann::json resp = {
        {"error", {{"message", "Incorrect API key provided"}, {"type", "invalid_request_error"}}}
    };
    auto result = provider.parseResponse(resp);
    ASSERT_TRUE(result.error.find("Incorrect API key") != std::string::npos);
}

int main() {
    std::cout << "=== LLM Provider Tests ===\n";

    // AC-13: Anthropic
    TEST(anthropic_build_request);
    TEST(anthropic_build_request_with_tools);
    TEST(anthropic_build_stream_request);
    TEST(anthropic_parse_response);
    TEST(anthropic_parse_tool_use);
    TEST(anthropic_parse_error);
    TEST(anthropic_headers);
    TEST(anthropic_name);

    // AC-14: OpenAI
    TEST(openai_build_request);
    TEST(openai_build_request_with_tools);
    TEST(openai_parse_response);
    TEST(openai_parse_tool_call);
    TEST(openai_headers);
    TEST(openai_name);

    // AC-15: Custom base URL
    TEST(openai_custom_base_url);

    // AC-16: SSE Parser
    TEST(sse_parser_text_data);
    TEST(sse_parser_event_type);
    TEST(sse_parser_done);
    TEST(sse_parser_anthropic_stop);
    TEST(sse_parser_partial_lines);
    TEST(sse_parser_reset);

    // AC-17: Error messages
    TEST(anthropic_parse_401_error);
    TEST(openai_parse_error);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
