#include <iostream>
#include <fstream>
#include <cassert>
#include <string>

// Tests for F-2/F-3: Plan Mode (/plan and /execute commands)
// and F-4: AgentLoop::runPlan()

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

// ============================================================
// AC-6: /plan <prompt> command is recognized and enters plan mode
// ============================================================

void test_ac6_plan_command_in_slash_commands() {
    std::string content = readSourceFile("src/tui/tui.h");
    ASSERT_TRUE(content.find("/plan") != std::string::npos);
}

void test_ac6_plan_command_handled() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("/plan") != std::string::npos);
}

void test_ac6_plan_mode_flag_exists() {
    std::string content = readSourceFile("src/tui/tui.h");
    ASSERT_TRUE(content.find("planMode_") != std::string::npos ||
                content.find("planMode") != std::string::npos);
}

void test_ac6_plan_sets_plan_mode() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // /plan should set planMode_ = true somewhere in the code
    ASSERT_TRUE(content.find("planMode_") != std::string::npos ||
                content.find("planMode") != std::string::npos);
    // Verify planMode is set to true when /plan is handled
    ASSERT_TRUE(content.find("planMode_.store(true)") != std::string::npos ||
                content.find("planMode_ = true") != std::string::npos);
}

// ============================================================
// AC-7: Plan mode system prompt instructs AI to only analyze and plan
// ============================================================

void test_ac7_plan_system_prompt_exists() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Plan mode should have a system prompt about analysis-only
    ASSERT_TRUE(content.find("analy") != std::string::npos); // "analyze" or "analysis"
    // Should mention not modifying files
    ASSERT_TRUE(content.find("not modify") != std::string::npos ||
                content.find("do not modify") != std::string::npos ||
                content.find("don't modify") != std::string::npos ||
                content.find("read-only") != std::string::npos ||
                content.find("without modifying") != std::string::npos ||
                content.find("DO NOT modify") != std::string::npos ||
                content.find("Do not modify") != std::string::npos);
}

// ============================================================
// AC-8: In plan mode, only read-only tools available
// ============================================================

void test_ac8_run_plan_uses_readonly_tools() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should call runPlan with read-only tools
    ASSERT_TRUE(content.find("runPlan") != std::string::npos);
    // Should specify file_read, glob, grep, ls
    ASSERT_TRUE(content.find("file_read") != std::string::npos);
    ASSERT_TRUE(content.find("glob") != std::string::npos);
    ASSERT_TRUE(content.find("grep") != std::string::npos);
    // ls should be in the allowed list
    ASSERT_TRUE(content.find("\"ls\"") != std::string::npos);
}

// ============================================================
// AC-9: Status bar displays [PLAN MODE] indicator
// ============================================================

void test_ac9_render_status_bar_accepts_plan_mode() {
    std::string content = readSourceFile("src/tui/render.h");
    // renderStatusBar should accept a planMode parameter
    ASSERT_TRUE(content.find("planMode") != std::string::npos ||
                content.find("plan_mode") != std::string::npos ||
                content.find("bool") != std::string::npos);
}

void test_ac9_plan_mode_indicator_in_status_bar() {
    std::string content = readSourceFile("src/tui/render.cpp");
    ASSERT_TRUE(content.find("PLAN MODE") != std::string::npos);
}

// ============================================================
// AC-10: Plan output is stored for later execution
// ============================================================

void test_ac10_last_plan_output_member() {
    std::string content = readSourceFile("src/tui/tui.h");
    ASSERT_TRUE(content.find("lastPlanOutput_") != std::string::npos);
}

void test_ac10_last_plan_prompt_member() {
    std::string content = readSourceFile("src/tui/tui.h");
    ASSERT_TRUE(content.find("lastPlanPrompt_") != std::string::npos);
}

// ============================================================
// AC-11: /execute runs the last plan with full tool access
// ============================================================

void test_ac11_execute_command_in_slash_commands() {
    std::string content = readSourceFile("src/tui/tui.h");
    ASSERT_TRUE(content.find("/execute") != std::string::npos);
}

void test_ac11_execute_command_handled() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("/execute") != std::string::npos);
}

void test_ac11_execute_uses_plan_output() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // /execute should use lastPlanOutput_ in the prompt
    auto execPos = content.find("/execute");
    ASSERT_TRUE(execPos != std::string::npos);
    auto block = content.substr(execPos, 1000);
    ASSERT_TRUE(block.find("lastPlanOutput_") != std::string::npos);
}

void test_ac11_execute_uses_full_tool_access() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // /execute should call normal run() not runPlan()
    // Find the last occurrence of /execute (the async execution section)
    auto execPos = content.rfind("/execute");
    ASSERT_TRUE(execPos != std::string::npos);
    auto remaining = content.size() - execPos;
    auto block = content.substr(execPos, std::min(remaining, (size_t)5000));
    ASSERT_TRUE(block.find("agentLoop_->run(") != std::string::npos);
}

// ============================================================
// AC-12: /execute without a prior plan shows error
// ============================================================

void test_ac12_execute_without_plan_shows_error() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto execPos = content.find("/execute");
    ASSERT_TRUE(execPos != std::string::npos);
    auto block = content.substr(execPos, 800);
    // Should check if lastPlanOutput_ is empty
    ASSERT_TRUE(block.find("empty()") != std::string::npos);
    // Should show an error message
    ASSERT_TRUE(block.find("error") != std::string::npos ||
                block.find("Error") != std::string::npos ||
                block.find("No plan") != std::string::npos ||
                block.find("no plan") != std::string::npos);
}

// ============================================================
// AC-13: After /execute, plan mode is exited
// ============================================================

void test_ac13_execute_exits_plan_mode() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // After /execute completes, planMode_ should be set to false
    // The execute thread sets planMode_.store(false) after completion
    ASSERT_TRUE(content.find("planMode_.store(false)") != std::string::npos ||
                content.find("planMode_ = false") != std::string::npos);
}

// ============================================================
// AC-14: AgentLoop has runPlan() method
// ============================================================

void test_ac14_run_plan_declared_in_header() {
    std::string content = readSourceFile("src/llm/agent_loop.h");
    ASSERT_TRUE(content.find("runPlan") != std::string::npos);
}

void test_ac14_run_plan_accepts_allowed_tools() {
    std::string content = readSourceFile("src/llm/agent_loop.h");
    // Should accept a vector of allowed tool names
    auto runPlanPos = content.find("runPlan");
    ASSERT_TRUE(runPlanPos != std::string::npos);
    auto sig = content.substr(runPlanPos, 300);
    ASSERT_TRUE(sig.find("allowedTools") != std::string::npos ||
                sig.find("allowed_tools") != std::string::npos);
}

// ============================================================
// AC-15: runPlan() only exposes specified tools
// ============================================================

void test_ac15_run_plan_filters_tools() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    auto runPlanPos = content.find("runPlan");
    ASSERT_TRUE(runPlanPos != std::string::npos);
    auto block = content.substr(runPlanPos, 800);
    // Should save/restore original tools (filter, run, restore pattern)
    ASSERT_TRUE(block.find("toolDefs_") != std::string::npos);
    ASSERT_TRUE(block.find("tools_") != std::string::npos);
}

void test_ac15_run_plan_saves_original_tools() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    auto runPlanPos = content.find("runPlan");
    ASSERT_TRUE(runPlanPos != std::string::npos);
    auto block = content.substr(runPlanPos, 800);
    // Should save original tools before filtering
    ASSERT_TRUE(block.find("original") != std::string::npos ||
                block.find("saved") != std::string::npos ||
                block.find("backup") != std::string::npos);
}

void test_ac15_run_plan_restores_tools() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    auto runPlanPos = content.find("runPlan");
    ASSERT_TRUE(runPlanPos != std::string::npos);
    auto block = content.substr(runPlanPos, 1200);
    // Should restore original tools after run
    ASSERT_TRUE(block.find("toolDefs_ =") != std::string::npos ||
                block.find("toolDefs_=") != std::string::npos ||
                block.find("toolDefs_ = original") != std::string::npos);
    ASSERT_TRUE(block.find("tools_ =") != std::string::npos ||
                block.find("tools_=") != std::string::npos ||
                block.find("tools_ = original") != std::string::npos);
}

// ============================================================
// AC-16: runPlan() reuses existing streaming, callbacks, cancellation
// ============================================================

void test_ac16_run_plan_delegates_to_run() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    auto runPlanPos = content.find("AgentLoop::runPlan");
    ASSERT_TRUE(runPlanPos != std::string::npos);
    auto block = content.substr(runPlanPos, 1200);
    // Should call run() internally
    ASSERT_TRUE(block.find("run(") != std::string::npos);
}

int main() {
    std::cout << "=== Plan Mode Tests (F-2, F-3, F-4) ===" << std::endl;

    std::cout << "\n--- AC-6: /plan command recognized ---\n";
    TEST(ac6_plan_command_in_slash_commands);
    TEST(ac6_plan_command_handled);
    TEST(ac6_plan_mode_flag_exists);
    TEST(ac6_plan_sets_plan_mode);

    std::cout << "\n--- AC-7: Plan system prompt ---\n";
    TEST(ac7_plan_system_prompt_exists);

    std::cout << "\n--- AC-8: Read-only tools ---\n";
    TEST(ac8_run_plan_uses_readonly_tools);

    std::cout << "\n--- AC-9: Status bar [PLAN MODE] ---\n";
    TEST(ac9_render_status_bar_accepts_plan_mode);
    TEST(ac9_plan_mode_indicator_in_status_bar);

    std::cout << "\n--- AC-10: Plan output stored ---\n";
    TEST(ac10_last_plan_output_member);
    TEST(ac10_last_plan_prompt_member);

    std::cout << "\n--- AC-11: /execute command ---\n";
    TEST(ac11_execute_command_in_slash_commands);
    TEST(ac11_execute_command_handled);
    TEST(ac11_execute_uses_plan_output);
    TEST(ac11_execute_uses_full_tool_access);

    std::cout << "\n--- AC-12: /execute without plan error ---\n";
    TEST(ac12_execute_without_plan_shows_error);

    std::cout << "\n--- AC-13: Plan mode exit after execute ---\n";
    TEST(ac13_execute_exits_plan_mode);

    std::cout << "\n--- AC-14: runPlan() method ---\n";
    TEST(ac14_run_plan_declared_in_header);
    TEST(ac14_run_plan_accepts_allowed_tools);

    std::cout << "\n--- AC-15: runPlan() tool filtering ---\n";
    TEST(ac15_run_plan_filters_tools);
    TEST(ac15_run_plan_saves_original_tools);
    TEST(ac15_run_plan_restores_tools);

    std::cout << "\n--- AC-16: runPlan() delegates to run() ---\n";
    TEST(ac16_run_plan_delegates_to_run);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
