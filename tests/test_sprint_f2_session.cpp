#include <iostream>
#include <cstdlib>
#include <cmath>
#include <cassert>
#include <fstream>
#include <unistd.h>
#include "session/session.h"

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
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b)

static std::string testDbPath;

static void setupDb() {
    char tmpl[] = "/tmp/opencode_test_f2_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (d) testDbPath = std::string(d) + "/test.db";
    else testDbPath = "/tmp/opencode_test_f2.db";
}

static void cleanupDb() {
    unlink(testDbPath.c_str());
    size_t pos = testDbPath.rfind('/');
    if (pos != std::string::npos) {
        std::string dir = testDbPath.substr(0, pos);
        rmdir(dir.c_str());
    }
}

// AC-4: estimateTokens() returns (total_chars / 4) * 1.2
void test_estimate_tokens_basic() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    sm.createSession();

    // Add a message with known content size
    Message msg;
    msg.role = "user";
    msg.content = std::string(400, 'a'); // 400 chars
    sm.addMessage(msg, sm.currentSessionId());

    // Expected: (400 / 4) * 1.2 = 120
    double tokens = sm.estimateTokens();
    ASSERT_TRUE(std::abs(tokens - 120.0) < 1.0);
}

void test_estimate_tokens_multiple_messages() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    sm.createSession();

    // Add two messages: 400 + 200 = 600 chars total
    Message msg1;
    msg1.role = "user";
    msg1.content = std::string(400, 'a');
    sm.addMessage(msg1, sm.currentSessionId());

    Message msg2;
    msg2.role = "assistant";
    msg2.content = std::string(200, 'b');
    sm.addMessage(msg2, sm.currentSessionId());

    // Expected: (600 / 4) * 1.2 = 180
    double tokens = sm.estimateTokens();
    ASSERT_TRUE(std::abs(tokens - 180.0) < 1.0);
}

void test_estimate_tokens_empty_history() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    sm.createSession();

    double tokens = sm.estimateTokens();
    ASSERT_TRUE(std::abs(tokens - 0.0) < 0.01);
}

// AC-5: getRecentMessages returns most recent messages fitting within token limit
void test_get_recent_messages_all_fit() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    sm.createSession();

    // Add small messages
    for (int i = 0; i < 3; i++) {
        Message msg;
        msg.role = "user";
        msg.content = "short msg " + std::to_string(i);
        sm.addMessage(msg, sm.currentSessionId());
    }

    // With a large token limit, all should be returned
    auto recent = sm.getRecentMessages(100000);
    ASSERT_EQ(static_cast<int>(recent.size()), 3);
}

void test_get_recent_messages_limited() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    sm.createSession();

    // Add messages with known sizes
    for (int i = 0; i < 10; i++) {
        Message msg;
        msg.role = "user";
        msg.content = std::string(100, 'x'); // each ~30 tokens ((100/4)*1.2)
        sm.addMessage(msg, sm.currentSessionId());
    }

    // Limit to ~60 tokens -> should fit ~2 messages (each ~30 tokens)
    auto recent = sm.getRecentMessages(60);
    ASSERT_TRUE(recent.size() >= 1);
    ASSERT_TRUE(recent.size() <= 3);
}

void test_get_recent_messages_newest_first_order() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    sm.createSession();

    Message msg1; msg1.role = "user"; msg1.content = "first";
    sm.addMessage(msg1, sm.currentSessionId());

    Message msg2; msg2.role = "user"; msg2.content = "second";
    sm.addMessage(msg2, sm.currentSessionId());

    Message msg3; msg3.role = "user"; msg3.content = "third";
    sm.addMessage(msg3, sm.currentSessionId());

    // Get recent with small limit - should get the last message(s)
    auto recent = sm.getRecentMessages(100000);
    ASSERT_TRUE(recent.size() == 3);
    // Messages should be in chronological order (oldest to newest)
    ASSERT_TRUE(recent.back().content.get<std::string>() == "third");
}

// AC-6: estimateTokens handles both string and JSON content
void test_estimate_tokens_json_content() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    sm.createSession();

    Message msg;
    msg.role = "assistant";
    // JSON content (array of content blocks)
    msg.content = nlohmann::json::array({
        {{"type", "text"}, {"text", "hello world"}},
        {{"type", "tool_use"}, {"id", "123"}, {"name", "bash"}, {"input", {{"command", "ls"}}}}
    });
    sm.addMessage(msg, sm.currentSessionId());

    double tokens = sm.estimateTokens();
    ASSERT_TRUE(tokens > 0);
}

// Source verification tests
void test_session_h_has_estimateTokens() {
    std::ifstream file("src/session/session.h");
    if (!file.is_open()) file.open("../src/session/session.h");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("estimateTokens") != std::string::npos);
}

void test_session_h_has_getRecentMessages() {
    std::ifstream file("src/session/session.h");
    if (!file.is_open()) file.open("../src/session/session.h");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("getRecentMessages") != std::string::npos);
}

void test_session_cpp_has_token_formula() {
    std::ifstream file("src/session/session.cpp");
    if (!file.is_open()) file.open("../src/session/session.cpp");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    // Check for the formula components
    ASSERT_TRUE(content.find("1.2") != std::string::npos);
}

int main() {
    setupDb();

    std::cout << "=== F-2: Session Token Estimation and Recent Messages Tests ===\n";

    std::cout << "\n--- AC-4: estimateTokens() ---\n";
    TEST(estimate_tokens_basic);
    TEST(estimate_tokens_multiple_messages);
    TEST(estimate_tokens_empty_history);

    std::cout << "\n--- AC-5: getRecentMessages() ---\n";
    TEST(get_recent_messages_all_fit);
    TEST(get_recent_messages_limited);
    TEST(get_recent_messages_newest_first_order);

    std::cout << "\n--- AC-6: JSON content handling ---\n";
    TEST(estimate_tokens_json_content);

    std::cout << "\n--- Source verification ---\n";
    TEST(session_h_has_estimateTokens);
    TEST(session_h_has_getRecentMessages);
    TEST(session_cpp_has_token_formula);

    cleanupDb();

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
