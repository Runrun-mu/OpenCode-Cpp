#include <iostream>
#include <cassert>
#include <climits>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <regex>

// Test the scroll logic for the FTXUI native scrolling approach
// F-1: Replace element slicing with vscroll_indicator | yframe | flex + focus

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
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + " (got " + std::to_string(a) + " vs " + std::to_string(b) + ")")

// Helper to read source file
static std::string readSourceFile(const std::string& relativePath) {
    std::ifstream file(relativePath);
    if (!file.is_open()) file.open("../" + relativePath);
    if (!file.is_open()) throw std::runtime_error("Cannot open " + relativePath);
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

// ============================================================
// F-1 AC-1: Messages fully visible - uses FTXUI native scrolling
// ============================================================

void test_ac1_uses_vscroll_indicator() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("vscroll_indicator") != std::string::npos);
}

void test_ac1_uses_yframe() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("yframe") != std::string::npos);
}

void test_ac1_no_element_slicing() {
    // Old element slicing used visibleElements vector - should be removed
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("visibleElements") == std::string::npos);
}

void test_ac1_no_manual_totalLines() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("totalLines") == std::string::npos);
}

void test_ac1_no_maxScroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("maxScroll") == std::string::npos);
}

void test_ac1_no_currentOffset() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("currentOffset") == std::string::npos);
}

void test_ac1_no_scrollOffset_atomic() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("scrollOffset") == std::string::npos);
}

void test_ac1_no_viewportHeight_atomic() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("viewportHeight") == std::string::npos);
}

// ============================================================
// F-1 AC-2: Auto-scroll to bottom during AI streaming
// ============================================================

void test_ac2_focus_on_last_element() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should use focus on last element for auto-scroll
    ASSERT_TRUE(content.find("focus") != std::string::npos);
    ASSERT_TRUE(content.find("chatElements.back()") != std::string::npos);
}

void test_ac2_scrollToBottom_flag_exists() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("scrollToBottom") != std::string::npos);
}

void test_ac2_screen_post_in_ontoken() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto onTokenPos = content.find("callbacks.onToken");
    ASSERT_TRUE(onTokenPos != std::string::npos);
    auto nextCallbackPos = content.find("callbacks.", onTokenPos + 15);
    std::string onTokenBlock = content.substr(onTokenPos, nextCallbackPos - onTokenPos);
    ASSERT_TRUE(onTokenBlock.find("screen.Post") != std::string::npos);
}

// ============================================================
// F-1 AC-3: Scroll up disables auto-scroll
// ============================================================

void test_ac3_mouse_wheel_up_disables_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto wheelUpPos = content.find("Mouse::WheelUp");
    ASSERT_TRUE(wheelUpPos != std::string::npos);
    auto blockEnd = content.find("}", wheelUpPos);
    auto block = content.substr(wheelUpPos, blockEnd - wheelUpPos);
    ASSERT_TRUE(block.find("scrollToBottom.store(false)") != std::string::npos);
}

void test_ac3_arrow_up_disables_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto arrowUpPos = content.find("Event::ArrowUp");
    ASSERT_TRUE(arrowUpPos != std::string::npos);
    auto blockEnd = content.find("Event::ArrowDown", arrowUpPos);
    auto block = content.substr(arrowUpPos, blockEnd - arrowUpPos);
    ASSERT_TRUE(block.find("scrollToBottom.store(false)") != std::string::npos);
}

void test_ac3_page_up_disables_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto pageUpPos = content.find("Event::PageUp");
    ASSERT_TRUE(pageUpPos != std::string::npos);
    auto blockEnd = content.find("Event::PageDown", pageUpPos);
    auto block = content.substr(pageUpPos, blockEnd - pageUpPos);
    ASSERT_TRUE(block.find("scrollToBottom.store(false)") != std::string::npos);
}

void test_ac3_home_disables_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto homePos = content.find("Event::Home");
    ASSERT_TRUE(homePos != std::string::npos);
    auto blockEnd = content.find("Event::End", homePos);
    auto block = content.substr(homePos, blockEnd - homePos);
    ASSERT_TRUE(block.find("scrollToBottom.store(false)") != std::string::npos);
}

// ============================================================
// F-1 AC-4: End key re-enables auto-scroll
// ============================================================

void test_ac4_end_key_enables_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto endPos = content.find("Event::End");
    ASSERT_TRUE(endPos != std::string::npos);
    auto blockEnd = content.find("return true;", endPos);
    auto block = content.substr(endPos, blockEnd - endPos);
    ASSERT_TRUE(block.find("scrollToBottom.store(true)") != std::string::npos);
}

// ============================================================
// F-1 AC-5: PageUp/PageDown scroll by viewport height (FTXUI native)
// ============================================================

void test_ac5_page_up_present() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("Event::PageUp") != std::string::npos);
}

void test_ac5_page_down_present() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("Event::PageDown") != std::string::npos);
}

// ============================================================
// Verify all event handlers are present
// ============================================================

void test_all_event_handlers_present() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("Event::ArrowUp") != std::string::npos);
    ASSERT_TRUE(content.find("Event::ArrowDown") != std::string::npos);
    ASSERT_TRUE(content.find("Event::PageUp") != std::string::npos);
    ASSERT_TRUE(content.find("Event::PageDown") != std::string::npos);
    ASSERT_TRUE(content.find("Mouse::WheelUp") != std::string::npos);
    ASSERT_TRUE(content.find("Mouse::WheelDown") != std::string::npos);
    ASSERT_TRUE(content.find("Event::Home") != std::string::npos);
    ASSERT_TRUE(content.find("Event::End") != std::string::npos);
}

void test_ontoken_does_not_set_scroll_to_bottom() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto onTokenPos = content.find("callbacks.onToken");
    ASSERT_TRUE(onTokenPos != std::string::npos);
    auto nextCallbackPos = content.find("callbacks.", onTokenPos + 15);
    std::string onTokenBlock = content.substr(onTokenPos, nextCallbackPos - onTokenPos);
    ASSERT_TRUE(onTokenBlock.find("scrollToBottom.store(true)") == std::string::npos);
    ASSERT_TRUE(onTokenBlock.find("scrollToBottom = true") == std::string::npos);
}

void test_user_message_sets_auto_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // After handleInput, scrollToBottom should be set to true
    auto handleInputPos = content.find("handleInput(msg)");
    ASSERT_TRUE(handleInputPos != std::string::npos);
    auto afterHandleInput = content.substr(handleInputPos, 200);
    ASSERT_TRUE(afterHandleInput.find("scrollToBottom.store(true)") != std::string::npos);
}

int main() {
    std::cout << "=== TUI Scroll Tests (F-1: FTXUI Native Scrolling) ===" << std::endl;

    std::cout << "\n--- AC-1: FTXUI native scrolling replaces element slicing ---\n";
    TEST(ac1_uses_vscroll_indicator);
    TEST(ac1_uses_yframe);
    TEST(ac1_no_element_slicing);
    TEST(ac1_no_manual_totalLines);
    TEST(ac1_no_maxScroll);
    TEST(ac1_no_currentOffset);
    TEST(ac1_no_scrollOffset_atomic);
    TEST(ac1_no_viewportHeight_atomic);

    std::cout << "\n--- AC-2: Auto-scroll during AI streaming ---\n";
    TEST(ac2_focus_on_last_element);
    TEST(ac2_scrollToBottom_flag_exists);
    TEST(ac2_screen_post_in_ontoken);

    std::cout << "\n--- AC-3: Scroll up disables auto-scroll ---\n";
    TEST(ac3_mouse_wheel_up_disables_auto_scroll);
    TEST(ac3_arrow_up_disables_auto_scroll);
    TEST(ac3_page_up_disables_auto_scroll);
    TEST(ac3_home_disables_auto_scroll);

    std::cout << "\n--- AC-4: End key re-enables auto-scroll ---\n";
    TEST(ac4_end_key_enables_auto_scroll);

    std::cout << "\n--- AC-5: PageUp/PageDown present ---\n";
    TEST(ac5_page_up_present);
    TEST(ac5_page_down_present);

    std::cout << "\n--- General event handler verification ---\n";
    TEST(all_event_handlers_present);
    TEST(ontoken_does_not_set_scroll_to_bottom);
    TEST(user_message_sets_auto_scroll);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
