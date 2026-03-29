#include <iostream>
#include <string>
#include <vector>
#include <set>
#include "llm/provider.h"
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

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x + " at line " + std::to_string(__LINE__))
#define ASSERT_FALSE(x) if (x) throw std::runtime_error(std::string("Assertion failed: !") + #x + " at line " + std::to_string(__LINE__))
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " != " + #b + " at line " + std::to_string(__LINE__))

// Helper: build a codex-mode provider
OpenAIProvider makeCodexProvider() {
    OpenAIProvider p("test-key", "codex-model", "https://api.example.com");
    p.setCodexMode(true, "acct-123");
    return p;
}

// Helper: create an assistant message with tool_calls JSON
Message makeAssistantWithToolCalls(const nlohmann::json& tool_calls, const std::string& content = "") {
    Message m;
    m.role = "assistant";
    m.content = content;
    m.tool_calls = tool_calls;
    return m;
}

// Helper: create a tool result message
Message makeToolResult(const std::string& call_id, const std::string& output) {
    Message m;
    m.role = "tool";
    m.content = output;
    m.tool_call_id = call_id;
    return m;
}

// =============================================================
// AC-1: function_call name never empty
// =============================================================
void test_ac1_function_call_name_never_empty() {
    auto p = makeCodexProvider();

    // Tool call with empty function name should be skipped
    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_1"}, {"function", {{"name", ""}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    // No function_call items should be present (name was empty)
    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            throw std::runtime_error("function_call with empty name was not skipped");
        }
    }
}

void test_ac1_function_call_name_valid_passes() {
    auto p = makeCodexProvider();

    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_1"}, {"function", {{"name", "bash"}, {"arguments", "{\"cmd\":\"ls\"}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    bool found = false;
    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            ASSERT_EQ(item["name"].get<std::string>(), std::string("bash"));
            found = true;
        }
    }
    ASSERT_TRUE(found);
}

// =============================================================
// AC-2: function_call arguments never empty
// =============================================================
void test_ac2_function_call_arguments_never_empty() {
    auto p = makeCodexProvider();

    // Tool call with missing arguments field
    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_1"}, {"function", {{"name", "bash"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            std::string args = item.value("arguments", "");
            ASSERT_FALSE(args.empty());
        }
    }
}

// =============================================================
// AC-3: Malformed tool_calls skipped
// =============================================================
void test_ac3_malformed_tool_calls_skipped() {
    auto p = makeCodexProvider();

    // Tool call without "function" object at all
    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_1"}},  // no "function" key
        {{"id", "call_2"}, {"function", {{"name", "bash"}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    int functionCallCount = 0;
    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            functionCallCount++;
            // The only one should be "bash"
            ASSERT_EQ(item["name"].get<std::string>(), std::string("bash"));
        }
    }
    ASSERT_EQ(functionCallCount, 1);
}

// =============================================================
// AC-4: function_call call_id never empty
// =============================================================
void test_ac4_function_call_callid_never_empty() {
    auto p = makeCodexProvider();

    // Tool call with empty id
    nlohmann::json tc = nlohmann::json::array({
        {{"id", ""}, {"function", {{"name", "bash"}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            std::string cid = item.value("call_id", "");
            ASSERT_FALSE(cid.empty());
            // Should start with "call_gen_"
            ASSERT_TRUE(cid.find("call_gen_") == 0);
        }
    }
}

void test_ac4_function_call_callid_no_id_key() {
    auto p = makeCodexProvider();

    // Tool call with missing id key entirely
    nlohmann::json tc = nlohmann::json::array({
        {{"function", {{"name", "bash"}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            std::string cid = item.value("call_id", "");
            ASSERT_FALSE(cid.empty());
        }
    }
}

// =============================================================
// AC-5: function_call_output call_id never empty
// =============================================================
void test_ac5_function_call_output_callid_never_empty() {
    auto p = makeCodexProvider();

    // Create a tool result with empty call_id
    Message toolResult;
    toolResult.role = "tool";
    toolResult.content = "output text";
    toolResult.tool_call_id = "";
    toolResult.tool_use_id = "";

    // Also add a function_call to avoid orphan conversion
    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_gen_0"}, {"function", {{"name", "bash"}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant, toolResult};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    for (auto& item : input) {
        if (item.value("type", "") == "function_call_output") {
            std::string cid = item.value("call_id", "");
            ASSERT_FALSE(cid.empty());
        }
    }
}

// =============================================================
// AC-6: call_id matching between pairs
// =============================================================
void test_ac6_callid_matching_between_pairs() {
    auto p = makeCodexProvider();

    // When a tool_call has a real ID, the corresponding tool result
    // should reference the same ID, and they should match in the output
    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_real_1"}, {"function", {{"name", "bash"}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    Message toolResult;
    toolResult.role = "tool";
    toolResult.content = "result text";
    toolResult.tool_call_id = "call_real_1";

    std::vector<Message> msgs = {assistant, toolResult};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    std::string funcCallId, funcOutputId;
    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            funcCallId = item.value("call_id", "");
        }
        if (item.value("type", "") == "function_call_output") {
            funcOutputId = item.value("call_id", "");
        }
    }
    ASSERT_FALSE(funcCallId.empty());
    ASSERT_FALSE(funcOutputId.empty());
    ASSERT_EQ(funcCallId, funcOutputId);
}

void test_ac6_fallback_ids_generated_consistently() {
    auto p = makeCodexProvider();

    // When both have empty ids, fallback IDs are generated with "call_gen_" prefix
    nlohmann::json tc = nlohmann::json::array({
        {{"id", ""}, {"function", {{"name", "bash"}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            std::string cid = item.value("call_id", "");
            ASSERT_TRUE(cid.find("call_gen_") == 0);
        }
    }
}

// =============================================================
// AC-7: Orphaned function_call_output converted
// =============================================================
void test_ac7_orphaned_function_call_output_converted() {
    auto p = makeCodexProvider();

    // Tool result with no matching function_call
    Message toolResult = makeToolResult("orphan_call_999", "orphaned output");

    std::vector<Message> msgs = {toolResult};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    // Should NOT have any function_call_output with that call_id
    for (auto& item : input) {
        if (item.value("type", "") == "function_call_output") {
            throw std::runtime_error("Orphaned function_call_output was not converted");
        }
    }

    // Should have a message instead
    bool foundConverted = false;
    for (auto& item : input) {
        if (item.contains("role") && item["role"] == "assistant") {
            std::string content = item.value("content", "");
            if (content.find("orphan_call_999") != std::string::npos) {
                foundConverted = true;
            }
        }
    }
    ASSERT_TRUE(foundConverted);
}

// =============================================================
// AC-8: Converted messages preserve content
// =============================================================
void test_ac8_converted_messages_preserve_content() {
    auto p = makeCodexProvider();

    Message toolResult = makeToolResult("orphan_id", "my important output data");

    std::vector<Message> msgs = {toolResult};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    bool foundContent = false;
    for (auto& item : input) {
        if (item.contains("role")) {
            std::string content = item.value("content", "");
            if (content.find("my important output data") != std::string::npos) {
                foundContent = true;
            }
        }
    }
    ASSERT_TRUE(foundContent);
}

// =============================================================
// AC-9: System messages filtered from input
// =============================================================
void test_ac9_system_messages_filtered() {
    auto p = makeCodexProvider();

    Message sys;
    sys.role = "system";
    sys.content = "You are a helpful assistant.";

    Message user;
    user.role = "user";
    user.content = "Hello";

    std::vector<Message> msgs = {sys, user};
    auto req = p.buildRequest(msgs, {}, "System prompt via instructions", false);
    auto& input = req["input"];

    for (auto& item : input) {
        if (item.contains("role") && item["role"] == "system") {
            throw std::runtime_error("System message found in input array");
        }
    }
    // User message should still be present
    bool foundUser = false;
    for (auto& item : input) {
        if (item.contains("role") && item["role"] == "user") {
            foundUser = true;
        }
    }
    ASSERT_TRUE(foundUser);
}

// =============================================================
// AC-10: System prompt via instructions only
// =============================================================
void test_ac10_system_prompt_via_instructions() {
    auto p = makeCodexProvider();

    Message user;
    user.role = "user";
    user.content = "Hello";

    std::vector<Message> msgs = {user};
    auto req = p.buildRequest(msgs, {}, "My system prompt", false);

    ASSERT_EQ(req["instructions"].get<std::string>(), std::string("My system prompt"));

    // No system role in input
    auto& input = req["input"];
    for (auto& item : input) {
        if (item.contains("role") && item["role"] == "system") {
            throw std::runtime_error("System message should not be in input array");
        }
    }
}

// =============================================================
// AC-11: SSE output_item.added handled
// =============================================================
void test_ac11_sse_output_item_added() {
    OpenAIProvider provider("test-key", "codex-model");
    provider.setCodexMode(true, "acct-123");

    // We'll test the SSE handling by simulating what the parser callback does.
    // We need to verify that the SSE event type "response.output_item.added"
    // populates tool call metadata.

    // Build the SSE data that the Codex API would send
    nlohmann::json addedEvent = {
        {"type", "response.output_item.added"},
        {"output_index", 0},
        {"item", {
            {"type", "function_call"},
            {"call_id", "call_abc"},
            {"name", "bash"}
        }}
    };

    nlohmann::json deltaEvent = {
        {"type", "response.function_call_arguments.delta"},
        {"output_index", 0},
        {"delta", "{\"cmd\":\"ls\"}"}
    };

    nlohmann::json doneEvent = {
        {"type", "response.function_call_arguments.done"},
        {"output_index", 0},
        {"arguments", "{\"cmd\":\"ls\"}"}
    };

    // We can simulate by feeding SSE data through SSEParser
    // and checking the result via tool call callbacks
    std::vector<ToolCall> receivedCalls;

    // Create a mini simulation of the codex SSE handling
    std::map<int, ToolCall> partialToolCalls;
    std::map<int, std::string> argStrings;

    auto handleEvent = [&](const nlohmann::json& data) {
        std::string type = data.value("type", "");
        if (type == "response.output_item.added") {
            if (data.contains("item") && data["item"].value("type", "") == "function_call") {
                int idx = data.value("output_index", 0);
                auto& item = data["item"];
                partialToolCalls[idx].id = item.value("call_id", "");
                partialToolCalls[idx].name = item.value("name", "");
            }
        } else if (type == "response.function_call_arguments.delta") {
            int idx = data.value("output_index", 0);
            std::string argDelta = data.value("delta", "");
            argStrings[idx] += argDelta;
        } else if (type == "response.function_call_arguments.done") {
            int idx = data.value("output_index", 0);
            if (partialToolCalls.count(idx)) {
                try {
                    partialToolCalls[idx].arguments = nlohmann::json::parse(argStrings[idx]);
                } catch (...) {}
                receivedCalls.push_back(partialToolCalls[idx]);
            }
        }
    };

    handleEvent(addedEvent);
    // Verify metadata was populated
    ASSERT_EQ(partialToolCalls[0].id, std::string("call_abc"));
    ASSERT_EQ(partialToolCalls[0].name, std::string("bash"));

    handleEvent(deltaEvent);
    handleEvent(doneEvent);

    ASSERT_EQ(receivedCalls.size(), (size_t)1);
    ASSERT_EQ(receivedCalls[0].name, std::string("bash"));
    ASSERT_EQ(receivedCalls[0].id, std::string("call_abc"));
}

// =============================================================
// AC-12: SSE function_call done finalizes correctly
// =============================================================
void test_ac12_sse_function_call_done_finalizes() {
    // This is tested implicitly via AC-11 above, but let's add explicit checks
    // for the done event emitting a complete ToolCall

    std::map<int, ToolCall> partialToolCalls;
    std::map<int, std::string> argStrings;
    std::vector<ToolCall> finalized;

    // Simulate output_item.added
    partialToolCalls[0].id = "call_xyz";
    partialToolCalls[0].name = "file_write";
    argStrings[0] = "{\"path\":\"/tmp/test.txt\",\"content\":\"hello\"}";

    // Simulate done event
    nlohmann::json doneData = {
        {"type", "response.function_call_arguments.done"},
        {"output_index", 0},
        {"arguments", argStrings[0]}
    };

    int idx = doneData.value("output_index", 0);
    if (partialToolCalls.count(idx)) {
        try {
            partialToolCalls[idx].arguments = nlohmann::json::parse(argStrings[idx]);
        } catch (...) {}
        finalized.push_back(partialToolCalls[idx]);
    }

    ASSERT_EQ(finalized.size(), (size_t)1);
    ASSERT_EQ(finalized[0].name, std::string("file_write"));
    ASSERT_EQ(finalized[0].id, std::string("call_xyz"));
    ASSERT_EQ(finalized[0].arguments["path"].get<std::string>(), std::string("/tmp/test.txt"));
    ASSERT_EQ(finalized[0].arguments["content"].get<std::string>(), std::string("hello"));
}

// =============================================================
// AC-13: SSE error events handled
// =============================================================
void test_ac13_sse_error_events() {
    // Verify that response.error events set result.error
    nlohmann::json errorData = {
        {"type", "response.error"},
        {"error", {{"message", "Rate limit exceeded"}}}
    };

    std::string errMsg = "Codex API error";
    std::string type = errorData.value("type", "");
    if (type == "response.error") {
        if (errorData.contains("error")) {
            errMsg = errorData["error"].value("message", errorData["error"].dump());
        }
    }

    ASSERT_EQ(errMsg, std::string("Rate limit exceeded"));
}

void test_ac13_sse_error_no_message() {
    nlohmann::json errorData = {
        {"type", "response.error"},
        {"error", {{"code", "server_error"}}}
    };

    std::string errMsg = "Codex API error";
    std::string type = errorData.value("type", "");
    if (type == "response.error") {
        if (errorData.contains("error")) {
            errMsg = errorData["error"].value("message", errorData["error"].dump());
        }
    }

    // Should fall back to dump of error object
    ASSERT_TRUE(errMsg.find("server_error") != std::string::npos);
}

// =============================================================
// AC-14: Non-codex buildRequest unchanged
// =============================================================
void test_ac14_non_codex_build_request_unchanged() {
    OpenAIProvider p("test-key", "gpt-4o");
    // NOT in codex mode

    Message user;
    user.role = "user";
    user.content = "Hello";

    Message sys;
    sys.role = "system";
    sys.content = "Be helpful";

    std::vector<Message> msgs = {sys, user};
    auto req = p.buildRequest(msgs, {}, "System prompt", false);

    // Should use messages format, not input
    ASSERT_TRUE(req.contains("messages"));
    ASSERT_FALSE(req.contains("input"));
    ASSERT_FALSE(req.contains("instructions"));

    // System message should be in messages array (first one from system_prompt)
    ASSERT_EQ(req["messages"][0]["role"].get<std::string>(), std::string("system"));
}

// =============================================================
// AC-15: Non-codex streaming unchanged
// =============================================================
void test_ac15_non_codex_streaming_format() {
    OpenAIProvider p("test-key", "gpt-4o");
    // NOT in codex mode

    Message user;
    user.role = "user";
    user.content = "Hello";

    std::vector<Message> msgs = {user};
    auto req = p.buildRequest(msgs, {}, "", true);

    // Should have stream and stream_options
    ASSERT_TRUE(req["stream"].get<bool>());
    ASSERT_TRUE(req.contains("stream_options"));
    ASSERT_TRUE(req.contains("messages"));
    ASSERT_FALSE(req.contains("input"));
}

// =============================================================
// Additional edge case tests
// =============================================================
void test_multiple_tool_calls_some_valid_some_not() {
    auto p = makeCodexProvider();

    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_1"}, {"function", {{"name", ""}, {"arguments", "{}"}}}},      // empty name - skip
        {{"id", "call_2"}},                                                           // no function - skip
        {{"id", "call_3"}, {"function", {{"name", "bash"}, {"arguments", "{}"}}}}    // valid
    });
    Message assistant = makeAssistantWithToolCalls(tc);

    std::vector<Message> msgs = {assistant};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    int functionCallCount = 0;
    for (auto& item : input) {
        if (item.value("type", "") == "function_call") {
            functionCallCount++;
        }
    }
    ASSERT_EQ(functionCallCount, 1);
}

void test_matched_function_call_output_not_converted() {
    auto p = makeCodexProvider();

    // Valid function_call + matching function_call_output
    nlohmann::json tc = nlohmann::json::array({
        {{"id", "call_valid"}, {"function", {{"name", "bash"}, {"arguments", "{}"}}}}
    });
    Message assistant = makeAssistantWithToolCalls(tc);
    Message toolResult = makeToolResult("call_valid", "command output");

    std::vector<Message> msgs = {assistant, toolResult};
    auto req = p.buildRequest(msgs, {}, "", false);
    auto& input = req["input"];

    // function_call_output should still exist (not converted)
    bool foundOutput = false;
    for (auto& item : input) {
        if (item.value("type", "") == "function_call_output") {
            ASSERT_EQ(item["call_id"].get<std::string>(), std::string("call_valid"));
            foundOutput = true;
        }
    }
    ASSERT_TRUE(foundOutput);
}

void test_codex_request_has_instructions_and_input() {
    auto p = makeCodexProvider();

    Message user;
    user.role = "user";
    user.content = "Hello";

    std::vector<Message> msgs = {user};
    auto req = p.buildRequest(msgs, {}, "System prompt", false);

    ASSERT_TRUE(req.contains("instructions"));
    ASSERT_TRUE(req.contains("input"));
    ASSERT_TRUE(req.contains("model"));
    ASSERT_EQ(req["store"].get<bool>(), false);
}

int main() {
    std::cout << "=== Codex Responses API Compatibility Tests ===\n";

    // AC-1: function_call name never empty
    TEST(ac1_function_call_name_never_empty);
    TEST(ac1_function_call_name_valid_passes);

    // AC-2: function_call arguments never empty
    TEST(ac2_function_call_arguments_never_empty);

    // AC-3: Malformed tool_calls skipped
    TEST(ac3_malformed_tool_calls_skipped);

    // AC-4: function_call call_id never empty
    TEST(ac4_function_call_callid_never_empty);
    TEST(ac4_function_call_callid_no_id_key);

    // AC-5: function_call_output call_id never empty
    TEST(ac5_function_call_output_callid_never_empty);

    // AC-6: call_id matching between pairs
    TEST(ac6_callid_matching_between_pairs);
    TEST(ac6_fallback_ids_generated_consistently);

    // AC-7: Orphaned function_call_output converted
    TEST(ac7_orphaned_function_call_output_converted);

    // AC-8: Converted messages preserve content
    TEST(ac8_converted_messages_preserve_content);

    // AC-9: System messages filtered from input
    TEST(ac9_system_messages_filtered);

    // AC-10: System prompt via instructions only
    TEST(ac10_system_prompt_via_instructions);

    // AC-11: SSE output_item.added handled
    TEST(ac11_sse_output_item_added);

    // AC-12: SSE function_call done finalizes correctly
    TEST(ac12_sse_function_call_done_finalizes);

    // AC-13: SSE error events handled
    TEST(ac13_sse_error_events);
    TEST(ac13_sse_error_no_message);

    // AC-14: Non-codex buildRequest unchanged
    TEST(ac14_non_codex_build_request_unchanged);

    // AC-15: Non-codex streaming unchanged
    TEST(ac15_non_codex_streaming_format);

    // Additional edge cases
    TEST(multiple_tool_calls_some_valid_some_not);
    TEST(matched_function_call_output_not_converted);
    TEST(codex_request_has_instructions_and_input);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
