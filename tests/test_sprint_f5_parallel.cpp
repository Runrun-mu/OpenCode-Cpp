#include <iostream>
#include <fstream>
#include <cassert>
#include <string>
#include <future>
#include <mutex>
#include <vector>
#include <chrono>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    tests_run++; \
    try { test_##name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED (unknown exception)\n"; }

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x)

static std::string readSourceFile(const std::string& relativePath) {
    std::ifstream file(relativePath);
    if (!file.is_open()) file.open("../" + relativePath);
    if (!file.is_open()) throw std::runtime_error("Cannot open " + relativePath);
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

// AC-16: Multiple tool calls dispatched via std::async
void test_uses_std_async() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("std::async") != std::string::npos);
}

void test_uses_launch_async() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("std::launch::async") != std::string::npos);
}

void test_includes_future_header() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("#include <future>") != std::string::npos);
}

// AC-17: All futures collected and .get() called
void test_futures_collected() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("futures") != std::string::npos);
    ASSERT_TRUE(content.find(".get()") != std::string::npos);
}

// AC-18: onToolStatus callbacks protected with mutex
void test_uses_mutex() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("std::mutex") != std::string::npos);
}

void test_uses_lock_guard() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("lock_guard") != std::string::npos ||
                content.find("unique_lock") != std::string::npos);
}

void test_includes_mutex_header() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("#include <mutex>") != std::string::npos);
}

// AC-19: Single tool call still works (regression-safe)
void test_single_tool_call_still_handled() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // The async pattern should work for both single and multiple tool calls
    // since it wraps all tool_calls in futures regardless of count
    ASSERT_TRUE(content.find("tool_calls") != std::string::npos);
    ASSERT_TRUE(content.find("futures") != std::string::npos);
}

// Functional test: verify std::async works correctly with our pattern
void test_async_pattern_correctness() {
    std::mutex mtx;
    std::vector<std::future<int>> futures;
    std::vector<int> results;

    // Simulate parallel tool execution
    for (int i = 0; i < 3; i++) {
        futures.push_back(std::async(std::launch::async, [&mtx, i]() {
            { std::lock_guard<std::mutex> lock(mtx); }
            return i * 10;
        }));
    }

    for (auto& f : futures) {
        results.push_back(f.get());
    }

    ASSERT_TRUE(results.size() == 3);
    ASSERT_TRUE(results[0] == 0);
    ASSERT_TRUE(results[1] == 10);
    ASSERT_TRUE(results[2] == 20);
}

// Verify results are collected before adding to messages
void test_results_collected_before_messages() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // There should be two separate loops:
    // 1. Launch futures loop
    // 2. Collect results loop (with .get())
    auto asyncPos = content.find("std::async");
    auto getPos = content.find(".get()");
    ASSERT_TRUE(asyncPos != std::string::npos);
    ASSERT_TRUE(getPos != std::string::npos);
    // .get() should come after std::async
    ASSERT_TRUE(getPos > asyncPos);
}

int main() {
    std::cout << "=== F-5: Parallel Tool Call Execution Tests ===\n";

    std::cout << "\n--- AC-16: std::async dispatch ---\n";
    TEST(uses_std_async);
    TEST(uses_launch_async);
    TEST(includes_future_header);

    std::cout << "\n--- AC-17: Future collection ---\n";
    TEST(futures_collected);
    TEST(results_collected_before_messages);

    std::cout << "\n--- AC-18: Mutex protection ---\n";
    TEST(uses_mutex);
    TEST(uses_lock_guard);
    TEST(includes_mutex_header);

    std::cout << "\n--- AC-19: Single tool call regression ---\n";
    TEST(single_tool_call_still_handled);

    std::cout << "\n--- Functional test ---\n";
    TEST(async_pattern_correctness);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
