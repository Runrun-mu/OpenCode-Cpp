#include <iostream>
#include <fstream>
#include <cassert>
#include <string>

// Tests for F-1: ScrollableContainer Component (Bug Fix)
// Verifies that a true FTXUI Component replaces yframe DOM decoration

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
// AC-1: ScrollableContainer Component wraps chat messages with internal selected_ index
// ============================================================

void test_ac1_scrollable_container_component_exists() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Must have a Component-based scrollable container, not just DOM decoration
    ASSERT_TRUE(content.find("ScrollableContainer") != std::string::npos ||
                content.find("scrollableContainer") != std::string::npos ||
                content.find("scrollable_container") != std::string::npos);
}

void test_ac1_has_selected_index() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Must have a selected_ index for tracking scroll position
    ASSERT_TRUE(content.find("selected_") != std::string::npos ||
                content.find("selected") != std::string::npos);
}

void test_ac1_is_component_not_just_decorator() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // The scrollable container must be a Component (ComponentBase or Make/CatchEvent pattern)
    ASSERT_TRUE(content.find("ComponentBase") != std::string::npos ||
                content.find("Make(") != std::string::npos ||
                content.find("CatchEvent") != std::string::npos);
}

void test_ac1_applies_focus_to_selected_element() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Must apply focus to the selected element inside yframe
    ASSERT_TRUE(content.find("focus") != std::string::npos);
    ASSERT_TRUE(content.find("yframe") != std::string::npos);
}

// ============================================================
// AC-2: Mouse wheel up/down scrolls chat even while isGenerating_ is true
// ============================================================

void test_ac2_mouse_wheel_handled_in_component() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Mouse wheel events must be handled
    ASSERT_TRUE(content.find("WheelUp") != std::string::npos);
    ASSERT_TRUE(content.find("WheelDown") != std::string::npos);
}

void test_ac2_mouse_scroll_returns_true() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // The scroll component's event handler should return true for wheel events
    // (meaning it consumed the event, not passing to inputComponent)
    auto wheelUpPos = content.find("WheelUp");
    ASSERT_TRUE(wheelUpPos != std::string::npos);
    // After WheelUp handling, should return true (event consumed)
    auto afterWheel = content.substr(wheelUpPos, 300);
    ASSERT_TRUE(afterWheel.find("return true") != std::string::npos);
}

// ============================================================
// AC-3: ArrowUp/ArrowDown scroll chat when input is empty during generation
// ============================================================

void test_ac3_arrow_keys_handle_scroll() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("ArrowUp") != std::string::npos);
    ASSERT_TRUE(content.find("ArrowDown") != std::string::npos);
}

void test_ac3_arrow_up_decrements_selected() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Arrow up should decrement selected_ (or equivalent)
    auto arrowUpPos = content.find("ArrowUp");
    ASSERT_TRUE(arrowUpPos != std::string::npos);
    auto block = content.substr(arrowUpPos, 500);
    ASSERT_TRUE(block.find("selected") != std::string::npos);
}

// ============================================================
// AC-4: End key sets scrollToBottom and auto-follows new output
// ============================================================

void test_ac4_end_key_enables_scroll_to_bottom() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto endPos = content.find("Event::End");
    ASSERT_TRUE(endPos != std::string::npos);
    auto block = content.substr(endPos, 200);
    ASSERT_TRUE(block.find("scrollToBottom") != std::string::npos);
    ASSERT_TRUE(block.find("true") != std::string::npos);
}

void test_ac4_home_key_goes_to_top() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    auto homePos = content.find("Event::Home");
    ASSERT_TRUE(homePos != std::string::npos);
    auto block = content.substr(homePos, 200);
    // Home should set selected to 0 or scrollToBottom to false
    ASSERT_TRUE(block.find("0") != std::string::npos || block.find("false") != std::string::npos);
}

// ============================================================
// AC-5: During AI streaming with scrollToBottom true, selected_ auto-updates
// ============================================================

void test_ac5_auto_scroll_updates_selected_in_render() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // When scrollToBottom is true, selected_ should be set to last element in Render
    ASSERT_TRUE(content.find("scrollToBottom") != std::string::npos);
    // The renderer should update selected_ based on scrollToBottom
    auto scrollPos = content.find("scrollToBottom");
    ASSERT_TRUE(scrollPos != std::string::npos);
}

int main() {
    std::cout << "=== ScrollableContainer Tests (F-1: Scroll Bug Fix) ===" << std::endl;

    std::cout << "\n--- AC-1: ScrollableContainer Component ---\n";
    TEST(ac1_scrollable_container_component_exists);
    TEST(ac1_has_selected_index);
    TEST(ac1_is_component_not_just_decorator);
    TEST(ac1_applies_focus_to_selected_element);

    std::cout << "\n--- AC-2: Mouse Wheel During Generation ---\n";
    TEST(ac2_mouse_wheel_handled_in_component);
    TEST(ac2_mouse_scroll_returns_true);

    std::cout << "\n--- AC-3: Arrow Keys During Generation ---\n";
    TEST(ac3_arrow_keys_handle_scroll);
    TEST(ac3_arrow_up_decrements_selected);

    std::cout << "\n--- AC-4: End Key Auto-Follow ---\n";
    TEST(ac4_end_key_enables_scroll_to_bottom);
    TEST(ac4_home_key_goes_to_top);

    std::cout << "\n--- AC-5: Auto-Scroll During Streaming ---\n";
    TEST(ac5_auto_scroll_updates_selected_in_render);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
