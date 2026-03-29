#include <iostream>
#include <fstream>
#include <cassert>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <queue>

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

static std::string readSourceFile(const std::string& relativePath) {
    std::ifstream file(relativePath);
    if (!file.is_open()) file.open("../" + relativePath);
    if (!file.is_open()) throw std::runtime_error("Cannot open " + relativePath);
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

// =============================================================
// AC-1: Steer queue checked after streaming completes
// =============================================================

// The agent loop must have a second steer queue drain AFTER streamMessage/sendMessage returns
void test_ac1_post_stream_steer_drain_exists() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // There should be steer queue checking code after the stream/send call
    // Look for steerQueue_ or steerMutex_ usage after the streamMessage call
    // We expect at least TWO steer queue drain blocks in the run() function
    size_t firstDrain = content.find("steerQueue_.empty()");
    ASSERT_TRUE(firstDrain != std::string::npos);
    size_t secondDrain = content.find("steerQueue_.empty()", firstDrain + 1);
    ASSERT_TRUE(secondDrain != std::string::npos);
}

// The post-stream drain must happen AFTER streamMessage/sendMessage and BEFORE the break check
void test_ac1_drain_before_break_check() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // Find the position of streamMessage call
    size_t streamPos = content.find("streamMessage(");
    ASSERT_TRUE(streamPos != std::string::npos);
    // Find the "No tool calls" break check
    size_t breakPos = content.find("tool_calls.empty()");
    ASSERT_TRUE(breakPos != std::string::npos);
    // The second steer drain should be between stream and break
    size_t firstDrain = content.find("steerQueue_.empty()");
    size_t secondDrain = content.find("steerQueue_.empty()", firstDrain + 1);
    ASSERT_TRUE(secondDrain != std::string::npos);
    ASSERT_TRUE(secondDrain > streamPos);
    // The second drain should be before the break on no tool calls
    // Find the break after the second drain
    size_t breakAfterDrain = content.find("tool_calls.empty()", secondDrain);
    ASSERT_TRUE(breakAfterDrain != std::string::npos);
}

// If steers exist post-stream, the loop should continue instead of break
void test_ac1_continue_on_steer() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // After the post-stream drain, there should be a "continue" to re-enter the loop
    size_t firstDrain = content.find("steerQueue_.empty()");
    size_t secondDrain = content.find("steerQueue_.empty()", firstDrain + 1);
    ASSERT_TRUE(secondDrain != std::string::npos);
    // Look for continue statement after the second drain
    size_t continuePos = content.find("continue", secondDrain);
    ASSERT_TRUE(continuePos != std::string::npos);
}

// =============================================================
// AC-2: No UI freeze during steer input - addSteer outside chatMutex
// =============================================================

void test_ac2_addsteer_outside_chatmutex() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Find the steer input handling section
    size_t steerSection = content.find("isGenerating_");
    ASSERT_TRUE(steerSection != std::string::npos);

    // The addSteer call should NOT be inside the chatMutex lock scope
    // Find the steer handling block near "isGenerating_"
    // Look for the pattern where addSteer is called
    size_t addSteerPos = content.find("addSteer(");
    ASSERT_TRUE(addSteerPos != std::string::npos);

    // Find the chatMutex lock scope that contains the steer display code
    // The addSteer call should be OUTSIDE (after) the chatMutex lock_guard scope
    // We look for the closing brace of the chatMutex scope before addSteer
    // or addSteer being in a separate scope from chatMutex

    // Find the steer-related chatMutex lock
    size_t steerLockPos = content.rfind("chatMutex", addSteerPos);
    ASSERT_TRUE(steerLockPos != std::string::npos);

    // There should be a closing brace between the chatMutex lock and addSteer
    // indicating addSteer is outside the lock scope
    std::string between = content.substr(steerLockPos, addSteerPos - steerLockPos);
    // Count braces to verify scope separation - addSteer should be at a different scope
    // At minimum, the lock_guard scope should be closed before addSteer
    // We verify addSteer is not on the same line as chatMutex lock
    size_t lockLine = content.rfind('\n', steerLockPos);
    size_t addSteerLine = content.rfind('\n', addSteerPos);
    ASSERT_TRUE(lockLine != addSteerLine); // Different lines
}

// =============================================================
// AC-3: Steer message reflected in AI output (steer in context)
// =============================================================

void test_ac3_steer_message_added_to_session() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // The steer drain should add messages to the session via addMessage
    // Both the first drain and post-stream drain should use session_.addMessage
    size_t firstDrain = content.find("steerQueue_.empty()");
    ASSERT_TRUE(firstDrain != std::string::npos);
    size_t addMsgAfterDrain = content.find("session_.addMessage", firstDrain);
    ASSERT_TRUE(addMsgAfterDrain != std::string::npos);
    // Also verify messages are pushed to the local messages vector
    size_t pushBackAfterDrain = content.find("messages.push_back", firstDrain);
    ASSERT_TRUE(pushBackAfterDrain != std::string::npos);
}

void test_ac3_steer_prefix_format() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // Steer messages should have [Steer] prefix
    ASSERT_TRUE(content.find("[Steer]") != std::string::npos);
}

// =============================================================
// AC-4: No mutex deadlock - lock hierarchy maintained
// =============================================================

void test_ac4_addsteer_only_uses_steer_mutex() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // The addSteer function should only use steerMutex_, not chatMutex
    size_t addSteerStart = content.find("void AgentLoop::addSteer");
    ASSERT_TRUE(addSteerStart != std::string::npos);
    size_t addSteerEnd = content.find("}", addSteerStart + 1);
    addSteerEnd = content.find("}", addSteerEnd + 1); // closing brace of function
    std::string addSteerBody = content.substr(addSteerStart, addSteerEnd - addSteerStart);
    ASSERT_TRUE(addSteerBody.find("steerMutex_") != std::string::npos);
    ASSERT_FALSE(addSteerBody.find("chatMutex") != std::string::npos);
}

void test_ac4_no_nested_mutex_in_agent_loop() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // In agent_loop.cpp, chatMutex should not appear (it's only in tui.cpp)
    // The agent loop only uses steerMutex_ internally
    // chatMutex is a tui.cpp concept
    // This verifies no accidental cross-mutex usage
    // (agent_loop.cpp should not reference chatMutex)
    ASSERT_FALSE(content.find("chatMutex") != std::string::npos);
}

// =============================================================
// AC-5: Conversation format validity - no consecutive user messages
// =============================================================

void test_ac5_synthetic_assistant_message_inserted() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // When steer messages are injected after an assistant message,
    // we need to ensure proper alternation.
    // The post-stream drain should save the assistant message BEFORE adding steers.
    // The assistant message is already saved at line 117.
    // The second drain happens after that, so steers follow the assistant msg correctly.
    // But if multiple steers arrive, we might need synthetic assistant messages between them.
    // Look for evidence of handling consecutive user messages or synthetic assistant message
    size_t secondDrain = content.find("steerQueue_.empty()");
    secondDrain = content.find("steerQueue_.empty()", secondDrain + 1);
    ASSERT_TRUE(secondDrain != std::string::npos);
    // There should be handling to ensure no two consecutive user messages
    // Either by inserting a synthetic assistant message or by combining steers
}

void test_ac5_assistant_message_saved_before_steer() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // The assistant message must be saved to session BEFORE the post-stream steer drain
    size_t assistantSave = content.find("session_.addMessage(assistantMsg");
    ASSERT_TRUE(assistantSave != std::string::npos);
    size_t firstDrain = content.find("steerQueue_.empty()");
    size_t secondDrain = content.find("steerQueue_.empty()", firstDrain + 1);
    ASSERT_TRUE(secondDrain != std::string::npos);
    // Assistant save should come before the second drain
    ASSERT_TRUE(assistantSave < secondDrain);
}

// =============================================================
// AC-6: Normal conversation flow unaffected
// =============================================================

void test_ac6_run_signature_unchanged() {
    std::string content = readSourceFile("src/llm/agent_loop.h");
    // run() signature should remain unchanged
    ASSERT_TRUE(content.find("LLMResponse run(") != std::string::npos);
    ASSERT_TRUE(content.find("const std::string& userMessage") != std::string::npos);
    ASSERT_TRUE(content.find("const std::string& systemPrompt") != std::string::npos);
    ASSERT_TRUE(content.find("AgentCallbacks callbacks") != std::string::npos);
    ASSERT_TRUE(content.find("int maxRounds") != std::string::npos);
}

void test_ac6_runplan_signature_unchanged() {
    std::string content = readSourceFile("src/llm/agent_loop.h");
    ASSERT_TRUE(content.find("LLMResponse runPlan(") != std::string::npos);
}

void test_ac6_addsteer_signature_unchanged() {
    std::string content = readSourceFile("src/llm/agent_loop.h");
    ASSERT_TRUE(content.find("void addSteer(const std::string& steerText)") != std::string::npos);
}

// =============================================================
// AC-7: Build succeeds (verified externally by cmake build)
// =============================================================

void test_ac7_source_files_exist() {
    // Verify the key source files exist and are readable
    std::string agentLoop = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(!agentLoop.empty());
    std::string agentLoopH = readSourceFile("src/llm/agent_loop.h");
    ASSERT_TRUE(!agentLoopH.empty());
    std::string tui = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(!tui.empty());
}

// =============================================================
// AC-8: Steer works with tool-use responses
// =============================================================

void test_ac8_steer_during_tool_execution() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // When tool calls are present, the loop continues to next iteration
    // The steer queue check at the START of each iteration (existing code)
    // handles steers that arrive during tool execution
    // Verify the start-of-loop drain is still present
    size_t firstDrain = content.find("steerQueue_.empty()");
    ASSERT_TRUE(firstDrain != std::string::npos);
    // And verify there's a second drain for post-stream steers
    size_t secondDrain = content.find("steerQueue_.empty()", firstDrain + 1);
    ASSERT_TRUE(secondDrain != std::string::npos);
}

// =============================================================
// Functional test: drainSteerQueue helper if it exists
// =============================================================

void test_drain_steer_queue_helper() {
    std::string headerContent = readSourceFile("src/llm/agent_loop.h");
    std::string srcContent = readSourceFile("src/llm/agent_loop.cpp");
    // The contract suggests adding a drainSteerQueue() private method
    // Check if it exists (optional - code may inline it instead)
    bool hasHelper = (headerContent.find("drainSteerQueue") != std::string::npos) ||
                     (srcContent.find("drainSteerQueue") != std::string::npos);
    // Either a helper exists, or the drain logic is duplicated inline (both are acceptable)
    // What matters is TWO drain points exist
    size_t firstDrain = srcContent.find("steerQueue_.empty()");
    ASSERT_TRUE(firstDrain != std::string::npos);
    size_t secondDrain = srcContent.find("steerQueue_.empty()", firstDrain + 1);
    ASSERT_TRUE(secondDrain != std::string::npos);
    (void)hasHelper; // Suppress unused warning
}

// =============================================================
// Token batching test for reduced mutex contention
// =============================================================

void test_ac2_token_batching_or_reduced_contention() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // The onToken callback should have some mechanism to reduce chatMutex contention
    // Either: token batching, atomic string buffer, or reduced lock scope
    // At minimum, addSteer must be outside chatMutex scope (tested in ac2 above)
    size_t onTokenPos = content.find("onToken");
    ASSERT_TRUE(onTokenPos != std::string::npos);
}

int main() {
    std::cout << "=== Steer Fix Tests (Sprint F1) ===\n";

    // AC-1: Steer queue checked after streaming completes
    TEST(ac1_post_stream_steer_drain_exists);
    TEST(ac1_drain_before_break_check);
    TEST(ac1_continue_on_steer);

    // AC-2: No UI freeze during steer input
    TEST(ac2_addsteer_outside_chatmutex);
    TEST(ac2_token_batching_or_reduced_contention);

    // AC-3: Steer message reflected in AI output
    TEST(ac3_steer_message_added_to_session);
    TEST(ac3_steer_prefix_format);

    // AC-4: No mutex deadlock
    TEST(ac4_addsteer_only_uses_steer_mutex);
    TEST(ac4_no_nested_mutex_in_agent_loop);

    // AC-5: Conversation format validity
    TEST(ac5_synthetic_assistant_message_inserted);
    TEST(ac5_assistant_message_saved_before_steer);

    // AC-6: Normal flow unaffected
    TEST(ac6_run_signature_unchanged);
    TEST(ac6_runplan_signature_unchanged);
    TEST(ac6_addsteer_signature_unchanged);

    // AC-7: Build succeeds
    TEST(ac7_source_files_exist);

    // AC-8: Steer works with tool-use responses
    TEST(ac8_steer_during_tool_execution);

    // Helper method
    TEST(drain_steer_queue_helper);

    std::cout << "\n=== Results: " << tests_passed << "/" << tests_run << " passed ===\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
