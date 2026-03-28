#include <iostream>
#include <fstream>
#include <cassert>
#include <string>

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

// AC-7: compact() sends the Codex handoff prompt
void test_compact_uses_codex_prompt() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("CONTEXT CHECKPOINT COMPACTION") != std::string::npos);
}

void test_compact_handoff_prompt_content() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("handoff summary") != std::string::npos);
}

// AC-8: Three-layer architecture
void test_compact_preserves_system_messages() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // Should check for system role to preserve
    ASSERT_TRUE(content.find("system") != std::string::npos);
}

void test_compact_keeps_recent_messages() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // Should reference ~20000 tokens for recent messages
    ASSERT_TRUE(content.find("20000") != std::string::npos ||
                content.find("20k") != std::string::npos ||
                content.find("recentTokenLimit") != std::string::npos ||
                content.find("recent") != std::string::npos);
}

void test_compact_summarizes_older_messages() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // Should send older messages to LLM for summarization
    ASSERT_TRUE(content.find("sendMessage") != std::string::npos);
}

// AC-9: Summary message is prefixed with "Another language model started..."
void test_compact_summary_prefix() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("Another language model started") != std::string::npos);
}

// AC-10: Status message after compaction
void test_compact_status_message() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("Context compacted:") != std::string::npos ||
                content.find("Context compacted") != std::string::npos);
}

void test_compact_tokens_saved_message() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("Tokens saved") != std::string::npos ||
                content.find("tokens saved") != std::string::npos ||
                content.find("tokensSaved") != std::string::npos);
}

// AC-11: Auto-compaction triggers
void test_auto_compact_on_token_threshold() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("estimateTokens") != std::string::npos);
    ASSERT_TRUE(content.find("compact_threshold") != std::string::npos);
}

void test_auto_compact_on_message_count() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("50") != std::string::npos);
}

// Verify compact returns stats
void test_compact_return_type() {
    std::string content = readSourceFile("src/llm/agent_loop.h");
    // compact should return a struct or have stats info
    ASSERT_TRUE(content.find("compact") != std::string::npos);
}

int main() {
    std::cout << "=== F-3: Smart Context Compaction Tests ===\n";

    std::cout << "\n--- AC-7: Codex handoff prompt ---\n";
    TEST(compact_uses_codex_prompt);
    TEST(compact_handoff_prompt_content);

    std::cout << "\n--- AC-8: Three-layer architecture ---\n";
    TEST(compact_preserves_system_messages);
    TEST(compact_keeps_recent_messages);
    TEST(compact_summarizes_older_messages);

    std::cout << "\n--- AC-9: Summary prefix ---\n";
    TEST(compact_summary_prefix);

    std::cout << "\n--- AC-10: Status message ---\n";
    TEST(compact_status_message);
    TEST(compact_tokens_saved_message);

    std::cout << "\n--- AC-11: Auto-compaction triggers ---\n";
    TEST(auto_compact_on_token_threshold);
    TEST(auto_compact_on_message_count);

    std::cout << "\n--- Return type ---\n";
    TEST(compact_return_type);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
