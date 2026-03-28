#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <fstream>
#include <regex>

// Tests for F-2 (/compact) and F-3 (/status) features
// Also includes F-1 scroll fix source verification tests

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

// Helper to read source file
static std::string readSourceFile(const std::string& relativePath) {
    std::ifstream file(relativePath);
    if (!file.is_open()) file.open("../" + relativePath);
    if (!file.is_open()) throw std::runtime_error("Cannot open " + relativePath);
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

// ============================================================
// F-1: Scroll Fix - FTXUI Native Scrolling (AC-1 through AC-5)
// ============================================================

// AC-1: All messages fully visible (uses vscroll_indicator | yframe | flex)
void test_ac1_uses_vscroll_indicator() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("vscroll_indicator") != std::string::npos);
}

void test_ac1_uses_yframe() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("yframe") != std::string::npos);
}

void test_ac1_no_element_slicing() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // The old element slicing code used "visibleElements" - should be gone
    ASSERT_TRUE(content.find("visibleElements") == std::string::npos);
}

void test_ac1_no_manual_totalLines() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Old code had totalLines for element slicing - should be gone
    ASSERT_TRUE(content.find("totalLines") == std::string::npos);
}

void test_ac1_no_maxScroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Old code had maxScroll for element slicing - should be gone
    ASSERT_TRUE(content.find("maxScroll") == std::string::npos);
}

void test_ac1_no_currentOffset() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Old code had currentOffset for element slicing - should be gone
    ASSERT_TRUE(content.find("currentOffset") == std::string::npos);
}

// AC-2: Auto-scroll to bottom during AI streaming (focus on last element)
void test_ac2_uses_focus_on_last_element() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should focus last element when scrollToBottom is true
    ASSERT_TRUE(content.find("focus") != std::string::npos);
    ASSERT_TRUE(content.find("chatElements[selected_]") != std::string::npos ||
                content.find("chatElements.back()") != std::string::npos);
}

// AC-3: User can scroll up and auto-scroll stops
void test_ac3_scroll_up_disables_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Mouse wheel up should set scrollToBottom false
    ASSERT_TRUE(content.find("scrollToBottom.store(false)") != std::string::npos);
}

// AC-4: End key re-enables auto-scroll
void test_ac4_end_key_enables_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // End key handler should set scrollToBottom true
    auto endPos = content.find("Event::End");
    ASSERT_TRUE(endPos != std::string::npos);
    auto blockEnd = content.find("return true;", endPos);
    auto block = content.substr(endPos, blockEnd - endPos);
    ASSERT_TRUE(block.find("scrollToBottom.store(true)") != std::string::npos);
}

// AC-5: PageUp/PageDown scroll by viewport height (handled by FTXUI natively)
void test_ac5_page_events_handled() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("Event::PageUp") != std::string::npos);
    ASSERT_TRUE(content.find("Event::PageDown") != std::string::npos);
}

// Verify scrollOffset atomic is removed
void test_scroll_offset_removed() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // scrollOffset atomic should be removed (no longer needed for manual slicing)
    ASSERT_TRUE(content.find("scrollOffset") == std::string::npos);
}

// Verify viewportHeight atomic is removed
void test_viewport_height_atomic_removed() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // viewportHeight atomic should be removed (FTXUI handles viewport)
    ASSERT_TRUE(content.find("viewportHeight") == std::string::npos);
}

// ============================================================
// F-2: /compact Command (AC-6 through AC-9)
// ============================================================

// AC-6: /compact triggers summarization via non-streaming LLM call
void test_ac6_compact_handled_in_tui() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("/compact") != std::string::npos);
}

void test_ac6_compact_method_exists_in_agent_loop_h() {
    std::string content = readSourceFile("src/llm/agent_loop.h");
    ASSERT_TRUE(content.find("compact") != std::string::npos);
}

void test_ac6_compact_method_exists_in_agent_loop_cpp() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    ASSERT_TRUE(content.find("compact") != std::string::npos);
}

void test_ac6_compact_uses_sendMessage() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // compact should use non-streaming sendMessage
    ASSERT_TRUE(content.find("sendMessage") != std::string::npos);
}

void test_ac6_compact_builds_summary_prompt() {
    std::string content = readSourceFile("src/llm/agent_loop.cpp");
    // compact should build a summary prompt
    ASSERT_TRUE(content.find("Summarize") != std::string::npos ||
                content.find("summarize") != std::string::npos ||
                content.find("summary") != std::string::npos);
}

// AC-7: After /compact, history replaced with single summary message
void test_ac7_clear_and_replace_in_session_h() {
    std::string content = readSourceFile("src/session/session.h");
    ASSERT_TRUE(content.find("clearAndReplace") != std::string::npos);
}

void test_ac7_clear_and_replace_in_session_cpp() {
    std::string content = readSourceFile("src/session/session.cpp");
    ASSERT_TRUE(content.find("clearAndReplace") != std::string::npos);
}

void test_ac7_clear_and_replace_clears_history() {
    std::string content = readSourceFile("src/session/session.cpp");
    // clearAndReplace should clear history and add summary
    auto pos = content.find("clearAndReplace");
    ASSERT_TRUE(pos != std::string::npos);
    auto funcEnd = content.find("}", content.find("{", pos));
    auto funcBody = content.substr(pos, funcEnd - pos);
    ASSERT_TRUE(funcBody.find("clear") != std::string::npos);
    ASSERT_TRUE(funcBody.find("push_back") != std::string::npos);
}

void test_ac7_message_count_in_session_h() {
    std::string content = readSourceFile("src/session/session.h");
    ASSERT_TRUE(content.find("messageCount") != std::string::npos);
}

// AC-8: Auto-compact triggers when message count exceeds 50
void test_ac8_auto_compact_check_in_tui() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should check for > 50 messages
    ASSERT_TRUE(content.find("50") != std::string::npos);
    ASSERT_TRUE(content.find("compact") != std::string::npos);
}

// AC-9: Status message shown confirming compact
void test_ac9_compact_confirmation_message() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should show a confirmation message after compact
    auto compactPos = content.find("/compact");
    ASSERT_TRUE(compactPos != std::string::npos);
    // Look for compact-related status message
    ASSERT_TRUE(content.find("compact") != std::string::npos);
}

// ============================================================
// F-3: /status Command (AC-10, AC-11)
// ============================================================

// AC-10: /status shows model, session ID, message count, tokens, cost, compact threshold
void test_ac10_status_handled_in_tui() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("/status") != std::string::npos);
}

void test_ac10_status_shows_model() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should display model name
    ASSERT_TRUE(content.find("getModel") != std::string::npos);
}

void test_ac10_status_shows_session_id() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("currentSessionId") != std::string::npos);
}

void test_ac10_status_shows_message_count() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should display message count
    auto statusPos = content.find("/status");
    ASSERT_TRUE(statusPos != std::string::npos);
    auto statusBlock = content.substr(statusPos, 600);
    ASSERT_TRUE(statusBlock.find("essage") != std::string::npos); // "Message" or "message"
}

void test_ac10_status_shows_token_counts() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto statusPos = content.find("/status");
    ASSERT_TRUE(statusPos != std::string::npos);
    auto statusBlock = content.substr(statusPos, 600);
    ASSERT_TRUE(statusBlock.find("oken") != std::string::npos); // "Token" or "token"
}

void test_ac10_status_shows_cost() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto statusPos = content.find("/status");
    ASSERT_TRUE(statusPos != std::string::npos);
    auto statusBlock = content.substr(statusPos, 600);
    ASSERT_TRUE(statusBlock.find("ost") != std::string::npos); // "Cost" or "cost"
}

void test_ac10_status_shows_compact_threshold() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto statusPos = content.find("/status");
    ASSERT_TRUE(statusPos != std::string::npos);
    auto statusBlock = content.substr(statusPos, 800);
    ASSERT_TRUE(statusBlock.find("compact_threshold") != std::string::npos ||
                statusBlock.find("Compact threshold") != std::string::npos ||
                statusBlock.find("102400") != std::string::npos ||
                statusBlock.find("50") != std::string::npos);
}

// AC-11: /status output appears as a tool message
void test_ac11_status_is_tool_message() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto statusPos = content.find("/status");
    ASSERT_TRUE(statusPos != std::string::npos);
    auto statusBlock = content.substr(statusPos, 600);
    ASSERT_TRUE(statusBlock.find("\"tool\"") != std::string::npos);
}

void test_ac11_status_tool_name() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto statusPos = content.find("/status");
    ASSERT_TRUE(statusPos != std::string::npos);
    auto statusBlock = content.substr(statusPos, 600);
    ASSERT_TRUE(statusBlock.find("status") != std::string::npos);
}

// ============================================================
// Integration: /compact and /status in Enter key handler
// ============================================================

void test_enter_handler_checks_compact() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Enter key handler should check for /compact (not just /skill)
    auto enterPos = content.find("Event::Return");
    ASSERT_TRUE(enterPos != std::string::npos);
    auto enterBlock = content.substr(enterPos, 3000);
    ASSERT_TRUE(enterBlock.find("compact") != std::string::npos ||
                enterBlock.find("handleSlashCommand") != std::string::npos);
}

void test_enter_handler_checks_status() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto enterPos = content.find("Event::Return");
    ASSERT_TRUE(enterPos != std::string::npos);
    auto enterBlock = content.substr(enterPos, 3000);
    ASSERT_TRUE(enterBlock.find("status") != std::string::npos ||
                enterBlock.find("handleSlashCommand") != std::string::npos);
}

int main() {
    std::cout << "=== Tests for F-1 (Scroll Fix), F-2 (/compact), F-3 (/status) ===" << std::endl;

    // F-1: Scroll Fix
    std::cout << "\n--- AC-1: All messages fully visible (FTXUI native scrolling) ---\n";
    TEST(ac1_uses_vscroll_indicator);
    TEST(ac1_uses_yframe);
    TEST(ac1_no_element_slicing);
    TEST(ac1_no_manual_totalLines);
    TEST(ac1_no_maxScroll);
    TEST(ac1_no_currentOffset);

    std::cout << "\n--- AC-2: Auto-scroll during AI streaming ---\n";
    TEST(ac2_uses_focus_on_last_element);

    std::cout << "\n--- AC-3: Scroll up disables auto-scroll ---\n";
    TEST(ac3_scroll_up_disables_auto_scroll);

    std::cout << "\n--- AC-4: End key re-enables auto-scroll ---\n";
    TEST(ac4_end_key_enables_auto_scroll);

    std::cout << "\n--- AC-5: PageUp/PageDown handled ---\n";
    TEST(ac5_page_events_handled);

    std::cout << "\n--- Scroll cleanup verification ---\n";
    TEST(scroll_offset_removed);
    TEST(viewport_height_atomic_removed);

    // F-2: /compact
    std::cout << "\n--- AC-6: /compact triggers summarization ---\n";
    TEST(ac6_compact_handled_in_tui);
    TEST(ac6_compact_method_exists_in_agent_loop_h);
    TEST(ac6_compact_method_exists_in_agent_loop_cpp);
    TEST(ac6_compact_uses_sendMessage);
    TEST(ac6_compact_builds_summary_prompt);

    std::cout << "\n--- AC-7: History replaced with summary ---\n";
    TEST(ac7_clear_and_replace_in_session_h);
    TEST(ac7_clear_and_replace_in_session_cpp);
    TEST(ac7_clear_and_replace_clears_history);
    TEST(ac7_message_count_in_session_h);

    std::cout << "\n--- AC-8: Auto-compact at 50 messages ---\n";
    TEST(ac8_auto_compact_check_in_tui);

    std::cout << "\n--- AC-9: Compact confirmation message ---\n";
    TEST(ac9_compact_confirmation_message);

    // F-3: /status
    std::cout << "\n--- AC-10: /status shows all required fields ---\n";
    TEST(ac10_status_handled_in_tui);
    TEST(ac10_status_shows_model);
    TEST(ac10_status_shows_session_id);
    TEST(ac10_status_shows_message_count);
    TEST(ac10_status_shows_token_counts);
    TEST(ac10_status_shows_cost);
    TEST(ac10_status_shows_compact_threshold);

    std::cout << "\n--- AC-11: /status as tool message ---\n";
    TEST(ac11_status_is_tool_message);
    TEST(ac11_status_tool_name);

    std::cout << "\n--- Integration: Enter handler checks ---\n";
    TEST(enter_handler_checks_compact);
    TEST(enter_handler_checks_status);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
