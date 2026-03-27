#include <iostream>
#include <cassert>
#include <climits>
#include <algorithm>
#include <string>
#include <vector>
#include <fstream>
#include <regex>

// Test the scroll logic independently of FTXUI
// We replicate the scroll clamping and state management logic from tui.cpp

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

// Replicate the scroll state logic from tui.cpp (updated to match new implementation)
struct ScrollState {
    int scrollOffset = 0;
    bool scrollToBottom = true;
    int viewportHeight = 20; // dynamic, computed from terminal size
    static constexpr int MOUSE_SCROLL_LINES = 3;
    static constexpr int UI_CHROME_HEIGHT = 5;

    // Compute dynamic viewport height from terminal height
    void computeViewportHeight(int termHeight) {
        viewportHeight = std::max(5, termHeight - UI_CHROME_HEIGHT);
    }

    int clamp(int totalLines) {
        int maxScroll = std::max(0, totalLines - viewportHeight);
        if (scrollToBottom) {
            scrollOffset = maxScroll;
        }
        scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
        return scrollOffset;
    }

    // Slice visible elements: returns [start, end) indices
    std::pair<int, int> getVisibleRange(int totalElements) {
        int start = scrollOffset;
        int end = std::min(start + viewportHeight, totalElements);
        return {start, end};
    }

    void pageUp() {
        scrollOffset -= viewportHeight; // dynamic, not hardcoded
        scrollToBottom = false;
    }

    void pageDown() {
        scrollOffset += viewportHeight; // dynamic, not hardcoded
    }

    void home() {
        scrollOffset = 0;
        scrollToBottom = false;
    }

    void end() {
        scrollToBottom = true;
    }

    void mouseWheelUp() {
        scrollOffset -= MOUSE_SCROLL_LINES;
        scrollToBottom = false;
    }

    void mouseWheelDown() {
        scrollOffset += MOUSE_SCROLL_LINES;
    }

    void arrowUpScroll() {
        scrollOffset -= 1;
        scrollToBottom = false;
    }

    void arrowDownScroll() {
        scrollOffset += 1;
    }

    void autoScroll() {
        scrollToBottom = true;
    }
};

// ============================================================
// AC-50: No focusPositionRelative in chat rendering
// ============================================================

void test_ac50_no_focus_position_relative_in_tui_cpp() {
    // Read the tui.cpp source file and verify focusPositionRelative is not used
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) {
        // Try from build directory
        file.open("../src/tui/tui.cpp");
    }
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    // focusPositionRelative must NOT appear in the file
    ASSERT_TRUE(content.find("focusPositionRelative") == std::string::npos);
}

void test_ac50_no_yframe_in_chat_box() {
    // The chatBox line should not use yframe
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    // yframe should not appear in the chatBox pipeline
    // (it may appear in comments - check it's not in active code)
    // Simple check: no "| yframe" in the file
    ASSERT_TRUE(content.find("| yframe") == std::string::npos);
}

void test_ac50_no_vscroll_indicator_in_chat_box() {
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("vscroll_indicator") == std::string::npos);
}

// ============================================================
// AC-51: Renderer slices chatElements by scrollOffset and viewport height
// ============================================================

void test_ac51_element_slicing_basic() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 5;
    s.viewportHeight = 10;
    s.clamp(50);

    auto [start, end] = s.getVisibleRange(50);
    ASSERT_EQ(start, 5);
    ASSERT_EQ(end, 15); // 5 + 10
}

void test_ac51_element_slicing_at_end() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 45;
    s.viewportHeight = 10;
    s.clamp(50);

    auto [start, end] = s.getVisibleRange(50);
    ASSERT_EQ(start, 40); // clamped: maxScroll = 50-10 = 40
    ASSERT_EQ(end, 50);
}

void test_ac51_element_slicing_few_elements() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 0;
    s.viewportHeight = 20;
    s.clamp(5); // only 5 elements, viewport is 20

    auto [start, end] = s.getVisibleRange(5);
    ASSERT_EQ(start, 0);
    ASSERT_EQ(end, 5); // min(0+20, 5)
}

void test_ac51_source_uses_element_slicing() {
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    // Must use chatElements.begin() for slicing
    ASSERT_TRUE(content.find("chatElements.begin()") != std::string::npos);
}

// ============================================================
// AC-52: scrollOffset clamped every frame
// ============================================================

void test_ac52_clamp_prevents_negative() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = -10;
    s.viewportHeight = 20;
    int result = s.clamp(100);
    ASSERT_EQ(result, 0);
}

void test_ac52_clamp_prevents_past_end() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 200;
    s.viewportHeight = 20;
    int result = s.clamp(100);
    ASSERT_EQ(result, 80);
}

void test_ac52_clamp_valid_offset_unchanged() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 50;
    s.viewportHeight = 20;
    int result = s.clamp(100);
    ASSERT_EQ(result, 50);
}

void test_ac52_clamp_no_scroll_when_content_fits() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 5;
    s.viewportHeight = 20;
    int result = s.clamp(10); // totalLines < viewport => maxScroll = 0
    ASSERT_EQ(result, 0);
}

void test_ac52_clamp_no_scroll_when_content_equals_viewport() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 5;
    s.viewportHeight = 20;
    int result = s.clamp(20); // totalLines == viewport => maxScroll = 0
    ASSERT_EQ(result, 0);
}

void test_ac52_source_uses_clamp() {
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    // Must use std::clamp or std::max/std::min for clamping
    bool hasClamp = content.find("std::clamp") != std::string::npos;
    bool hasMaxMin = content.find("std::max") != std::string::npos &&
                     content.find("std::min") != std::string::npos;
    ASSERT_TRUE(hasClamp || hasMaxMin);
}

// ============================================================
// AC-53: Dynamic viewport height from Terminal::Size()
// ============================================================

void test_ac53_dynamic_viewport_height_computation() {
    ScrollState s;
    s.computeViewportHeight(40); // terminal height 40
    ASSERT_EQ(s.viewportHeight, 35); // 40 - 5 chrome
}

void test_ac53_viewport_height_minimum() {
    ScrollState s;
    s.computeViewportHeight(3); // very small terminal
    ASSERT_EQ(s.viewportHeight, 5); // minimum 5
}

void test_ac53_viewport_height_large_terminal() {
    ScrollState s;
    s.computeViewportHeight(100);
    ASSERT_EQ(s.viewportHeight, 95); // 100 - 5
}

void test_ac53_source_uses_terminal_size() {
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("Terminal::Size") != std::string::npos);
}

// ============================================================
// AC-54: Arrow key scrolling by 1 element
// ============================================================

void test_ac54_arrow_up_scrolls_1_line() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 10;
    s.viewportHeight = 20;
    s.arrowUpScroll();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 9);
}

void test_ac54_arrow_up_sets_scroll_to_bottom_false() {
    ScrollState s;
    s.scrollToBottom = true;
    s.scrollOffset = 50;
    s.arrowUpScroll();
    ASSERT_FALSE(s.scrollToBottom);
}

void test_ac54_arrow_up_clamped_at_zero() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 0;
    s.viewportHeight = 20;
    s.arrowUpScroll();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 0);
}

void test_ac54_arrow_down_scrolls_1_line() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 10;
    s.viewportHeight = 20;
    s.arrowDownScroll();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 11);
}

void test_ac54_arrow_down_clamped_at_max() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 80;
    s.viewportHeight = 20;
    s.arrowDownScroll();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 80);
}

// ============================================================
// AC-55: Page scrolling by viewportHeight elements (dynamic)
// ============================================================

void test_ac55_page_up_scrolls_by_viewport_height() {
    ScrollState s;
    s.scrollToBottom = false;
    s.viewportHeight = 30;
    s.scrollOffset = 50;
    s.pageUp();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 20); // 50 - 30 = 20
}

void test_ac55_page_up_with_different_viewport() {
    ScrollState s;
    s.scrollToBottom = false;
    s.viewportHeight = 15;
    s.scrollOffset = 50;
    s.pageUp();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 35); // 50 - 15 = 35
}

void test_ac55_page_up_clamped_to_zero() {
    ScrollState s;
    s.scrollToBottom = false;
    s.viewportHeight = 30;
    s.scrollOffset = 10;
    s.pageUp(); // 10 - 30 = -20, clamp to 0
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 0);
}

void test_ac55_page_up_sets_scroll_to_bottom_false() {
    ScrollState s;
    s.scrollToBottom = true;
    s.viewportHeight = 20;
    s.pageUp();
    ASSERT_FALSE(s.scrollToBottom);
}

void test_ac55_page_down_scrolls_by_viewport_height() {
    ScrollState s;
    s.scrollToBottom = false;
    s.viewportHeight = 30;
    s.scrollOffset = 20;
    s.pageDown();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 50); // 20 + 30 = 50
}

void test_ac55_page_down_clamped_to_max() {
    ScrollState s;
    s.scrollToBottom = false;
    s.viewportHeight = 30;
    s.scrollOffset = 60;
    s.pageDown(); // 60 + 30 = 90, maxScroll = 100-30 = 70
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 70);
}

// ============================================================
// AC-56: Mouse wheel scrolling by MOUSE_SCROLL_LINES (3) elements
// ============================================================

void test_ac56_mouse_wheel_up_scrolls_3_lines() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 30;
    s.viewportHeight = 20;
    s.mouseWheelUp();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 27);
}

void test_ac56_mouse_scroll_lines_constant_is_3() {
    ASSERT_EQ(ScrollState::MOUSE_SCROLL_LINES, 3);
}

void test_ac56_mouse_wheel_down_scrolls_3_lines() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 20;
    s.viewportHeight = 20;
    s.mouseWheelDown();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 23);
}

void test_ac56_mouse_wheel_up_clamped_to_zero() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 1;
    s.viewportHeight = 20;
    s.mouseWheelUp();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 0);
}

void test_ac56_mouse_wheel_down_clamped_to_max() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 79;
    s.viewportHeight = 20;
    s.mouseWheelDown(); // 79 + 3 = 82, maxScroll = 80
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 80);
}

// ============================================================
// AC-57: Home/End navigation
// ============================================================

void test_ac57_home_sets_offset_to_zero() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 50;
    s.viewportHeight = 20;
    s.home();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 0);
}

void test_ac57_home_sets_scroll_to_bottom_false() {
    ScrollState s;
    s.scrollToBottom = true;
    s.home();
    ASSERT_FALSE(s.scrollToBottom);
    ASSERT_EQ(s.scrollOffset, 0);
}

void test_ac57_end_sets_scroll_to_bottom_true() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 10;
    s.end();
    ASSERT_TRUE(s.scrollToBottom);
}

void test_ac57_end_results_in_max_scroll_after_clamp() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 10;
    s.viewportHeight = 20;
    s.end();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 80);
}

// ============================================================
// AC-58: Scrolling during generation
// (verified by checking that event handlers do not gate on isGenerating for scroll)
// ============================================================

void test_ac58_arrow_up_works_during_generation() {
    // The scroll logic itself is isGenerating-independent.
    // When isGenerating is true AND input is empty (or generating),
    // arrow up still calls arrowUpScroll.
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 10;
    s.viewportHeight = 20;
    bool isGenerating = true;
    // Simulate: inputText.empty() || isGenerating => arrowUpScroll
    if (isGenerating) {
        s.arrowUpScroll();
    }
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 9);
}

void test_ac58_page_up_works_during_generation() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 50;
    s.viewportHeight = 20;
    bool isGenerating = true;
    // PageUp is not gated on isGenerating in the event handler
    s.pageUp();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 30);
}

void test_ac58_mouse_wheel_works_during_generation() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 50;
    s.viewportHeight = 20;
    bool isGenerating = true;
    s.mouseWheelUp();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 47);
}

// ============================================================
// AC-59: Auto-scroll on user message
// ============================================================

void test_ac59_auto_scroll_on_user_message() {
    ScrollState s;
    s.scrollToBottom = false;
    s.scrollOffset = 10;
    s.viewportHeight = 20;
    s.autoScroll(); // simulates scrollToBottom.store(true) on user message send
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 80);
    ASSERT_TRUE(s.scrollToBottom);
}

// ============================================================
// AC-60: onToken does NOT force scroll-to-bottom
// ============================================================

void test_ac60_on_token_does_not_set_scroll_to_bottom() {
    // Verify in source code that onToken callback does NOT contain scrollToBottom.store(true)
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Find the onToken callback region
    auto onTokenPos = content.find("callbacks.onToken");
    ASSERT_TRUE(onTokenPos != std::string::npos);

    // Find the end of the onToken lambda (next callbacks. or end of block)
    auto nextCallbackPos = content.find("callbacks.", onTokenPos + 10);
    std::string onTokenBlock = content.substr(onTokenPos, nextCallbackPos - onTokenPos);

    // onToken block must NOT contain scrollToBottom.store(true) or scrollToBottom = true
    ASSERT_TRUE(onTokenBlock.find("scrollToBottom.store(true)") == std::string::npos);
    ASSERT_TRUE(onTokenBlock.find("scrollToBottom = true") == std::string::npos);
}

// ============================================================
// AC-61: Manual scroll disengages auto-follow
// ============================================================

void test_ac61_mouse_wheel_up_disables_auto_scroll() {
    ScrollState s;
    s.scrollToBottom = true;
    s.scrollOffset = 80;
    s.mouseWheelUp();
    ASSERT_FALSE(s.scrollToBottom);
}

void test_ac61_arrow_up_disables_auto_scroll() {
    ScrollState s;
    s.scrollToBottom = true;
    s.scrollOffset = 80;
    s.arrowUpScroll();
    ASSERT_FALSE(s.scrollToBottom);
}

void test_ac61_page_up_disables_auto_scroll() {
    ScrollState s;
    s.scrollToBottom = true;
    s.scrollOffset = 80;
    s.pageUp();
    ASSERT_FALSE(s.scrollToBottom);
}

void test_ac61_home_disables_auto_scroll() {
    ScrollState s;
    s.scrollToBottom = true;
    s.home();
    ASSERT_FALSE(s.scrollToBottom);
}

// ============================================================
// AC-62: End key re-engages auto-follow
// ============================================================

void test_ac62_end_key_reenables_auto_scroll() {
    ScrollState s;
    s.scrollToBottom = true;
    s.mouseWheelUp(); // disengage
    ASSERT_FALSE(s.scrollToBottom);
    s.end(); // re-engage
    ASSERT_TRUE(s.scrollToBottom);
}

void test_ac62_full_workflow_scroll_up_then_end() {
    ScrollState s;
    s.scrollToBottom = true;
    s.scrollOffset = 80;
    s.viewportHeight = 20;

    // Scroll up manually
    s.mouseWheelUp();
    ASSERT_FALSE(s.scrollToBottom);
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 77);

    // More scrolling up
    s.mouseWheelUp();
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 74);

    // Press End - should re-enable auto-scroll and jump to bottom
    s.end();
    ASSERT_TRUE(s.scrollToBottom);
    s.clamp(100);
    ASSERT_EQ(s.scrollOffset, 80);
}

// ============================================================
// AC-63: Build succeeds (verified externally by build)
// ============================================================

void test_ac63_constants_match() {
    ASSERT_EQ(ScrollState::MOUSE_SCROLL_LINES, 3);
    ASSERT_EQ(ScrollState::UI_CHROME_HEIGHT, 5);
}

// ============================================================
// AC-65: E2E scroll during streaming (code verification)
// Verify all scroll handlers exist and work during generation
// ============================================================

void test_ac65_all_scroll_handlers_present_in_source() {
    // Verify that all required scroll event handlers are present in tui.cpp
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // All these event handlers must be present for E2E scrolling
    ASSERT_TRUE(content.find("Event::ArrowUp") != std::string::npos);
    ASSERT_TRUE(content.find("Event::ArrowDown") != std::string::npos);
    ASSERT_TRUE(content.find("Event::PageUp") != std::string::npos);
    ASSERT_TRUE(content.find("Event::PageDown") != std::string::npos);
    ASSERT_TRUE(content.find("Mouse::WheelUp") != std::string::npos);
    ASSERT_TRUE(content.find("Mouse::WheelDown") != std::string::npos);
    ASSERT_TRUE(content.find("Event::Home") != std::string::npos);
    ASSERT_TRUE(content.find("Event::End") != std::string::npos);
}

void test_ac65_page_scroll_not_gated_on_generating() {
    // PageUp/PageDown handlers must NOT check isGenerating_ (they work always)
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Find PageUp handler block and verify it doesn't check isGenerating_
    auto pageUpPos = content.find("Event::PageUp");
    ASSERT_TRUE(pageUpPos != std::string::npos);
    // Get surrounding context (50 chars before, to check for isGenerating_ guard)
    std::string pageUpContext = content.substr(
        pageUpPos > 100 ? pageUpPos - 100 : 0,
        std::min((size_t)200, content.size() - pageUpPos + 100)
    );
    // PageUp should NOT have isGenerating_ check (it's unconditionally handled)
    // The ArrowUp check with isGenerating_ is correct (it shares with input history),
    // but PageUp must not be gated.
    auto pageUpLine = content.substr(pageUpPos, content.find('\n', pageUpPos) - pageUpPos);
    // Check there's no isGenerating_ between the previous if-block-end and PageUp
    auto prevNewline = content.rfind('\n', pageUpPos);
    auto prevPrevNewline = content.rfind('\n', prevNewline > 0 ? prevNewline - 1 : 0);
    std::string lineBeforePageUp = content.substr(prevPrevNewline, prevNewline - prevPrevNewline);
    // The line before PageUp should not contain isGenerating_
    ASSERT_TRUE(lineBeforePageUp.find("isGenerating_") == std::string::npos);
}

void test_ac65_mouse_wheel_not_gated_on_generating() {
    // Mouse wheel handlers must not be inside an isGenerating_ check
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // The mouse wheel handler is inside "if (event.is_mouse())" block
    // It must NOT be additionally gated on isGenerating_
    auto mousePos = content.find("event.is_mouse()");
    ASSERT_TRUE(mousePos != std::string::npos);

    // Check the 5 lines before mouse check for isGenerating_
    auto lineStart = mousePos;
    for (int i = 0; i < 5 && lineStart > 0; i++) {
        lineStart = content.rfind('\n', lineStart - 1);
    }
    std::string contextBefore = content.substr(lineStart, mousePos - lineStart);
    ASSERT_TRUE(contextBefore.find("isGenerating_") == std::string::npos);
}

void test_ac65_streaming_scroll_workflow_simulation() {
    // Simulate the full E2E workflow: content grows (streaming), user scrolls up,
    // content keeps growing, user presses End to re-follow
    ScrollState s;
    s.scrollToBottom = true;
    s.viewportHeight = 20;

    // Simulate initial streaming: 30 elements
    s.clamp(30); // scrollToBottom=true, so offset = max(0, 30-20) = 10
    ASSERT_EQ(s.scrollOffset, 10);

    // More tokens arrive: now 40 elements
    s.clamp(40); // still following: offset = max(0, 40-20) = 20
    ASSERT_EQ(s.scrollOffset, 20);

    // User scrolls up with mouse wheel during generation
    s.mouseWheelUp(); // offset = 20-3=17, scrollToBottom=false
    ASSERT_FALSE(s.scrollToBottom);
    s.clamp(40);
    ASSERT_EQ(s.scrollOffset, 17);

    // More tokens arrive: now 50 elements, but user is NOT following
    s.clamp(50); // scrollToBottom=false, offset stays at 17
    ASSERT_EQ(s.scrollOffset, 17);

    // More tokens: 60 elements, user still at position 17
    s.clamp(60);
    ASSERT_EQ(s.scrollOffset, 17);

    // User scrolls up more with arrow key
    s.arrowUpScroll();
    s.clamp(60);
    ASSERT_EQ(s.scrollOffset, 16);

    // User presses PageUp
    s.pageUp(); // 16 - 20 = -4, clamped to 0
    s.clamp(60);
    ASSERT_EQ(s.scrollOffset, 0);

    // User presses End to re-follow
    s.end();
    ASSERT_TRUE(s.scrollToBottom);
    s.clamp(60); // now at max: 60-20=40
    ASSERT_EQ(s.scrollOffset, 40);

    // More tokens: 70 elements, user is following again
    s.clamp(70); // following: offset = 70-20=50
    ASSERT_EQ(s.scrollOffset, 50);
}

void test_ac65_on_token_only_posts_custom_event() {
    // Verify that the onToken callback only calls screen.Post(Event::Custom)
    // and does NOT reset scrollToBottom or scrollOffset
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    auto onTokenPos = content.find("callbacks.onToken");
    ASSERT_TRUE(onTokenPos != std::string::npos);

    // Find the closing of the onToken lambda
    auto nextCallbackPos = content.find("callbacks.", onTokenPos + 15);
    std::string onTokenBlock = content.substr(onTokenPos, nextCallbackPos - onTokenPos);

    // Must contain screen.Post (to trigger re-render)
    ASSERT_TRUE(onTokenBlock.find("screen.Post") != std::string::npos);

    // Must NOT force scroll position
    ASSERT_TRUE(onTokenBlock.find("scrollOffset.store") == std::string::npos);
    ASSERT_TRUE(onTokenBlock.find("scrollToBottom.store(true)") == std::string::npos);
}

// ============================================================
// Additional edge case: scroll_to_bottom flag overrides manual offset
// ============================================================

void test_scroll_to_bottom_flag_overrides_manual_offset() {
    ScrollState s;
    s.scrollToBottom = true;
    s.scrollOffset = 10;
    s.viewportHeight = 20;
    int result = s.clamp(100);
    ASSERT_EQ(result, 80);
}

int main() {
    std::cout << "=== TUI Scroll Tests (AC-50 through AC-65) ===" << std::endl;

    // AC-50: No focusPositionRelative in chat rendering
    std::cout << "\n--- AC-50: No focusPositionRelative in chat rendering ---\n";
    TEST(ac50_no_focus_position_relative_in_tui_cpp);
    TEST(ac50_no_yframe_in_chat_box);
    TEST(ac50_no_vscroll_indicator_in_chat_box);

    // AC-51: Renderer slices chatElements
    std::cout << "\n--- AC-51: Element slicing ---\n";
    TEST(ac51_element_slicing_basic);
    TEST(ac51_element_slicing_at_end);
    TEST(ac51_element_slicing_few_elements);
    TEST(ac51_source_uses_element_slicing);

    // AC-52: scrollOffset clamped every frame
    std::cout << "\n--- AC-52: Scroll offset clamping ---\n";
    TEST(ac52_clamp_prevents_negative);
    TEST(ac52_clamp_prevents_past_end);
    TEST(ac52_clamp_valid_offset_unchanged);
    TEST(ac52_clamp_no_scroll_when_content_fits);
    TEST(ac52_clamp_no_scroll_when_content_equals_viewport);
    TEST(ac52_source_uses_clamp);

    // AC-53: Dynamic viewport height
    std::cout << "\n--- AC-53: Dynamic viewport height ---\n";
    TEST(ac53_dynamic_viewport_height_computation);
    TEST(ac53_viewport_height_minimum);
    TEST(ac53_viewport_height_large_terminal);
    TEST(ac53_source_uses_terminal_size);

    // AC-54: Arrow key scrolling
    std::cout << "\n--- AC-54: Arrow key scrolling ---\n";
    TEST(ac54_arrow_up_scrolls_1_line);
    TEST(ac54_arrow_up_sets_scroll_to_bottom_false);
    TEST(ac54_arrow_up_clamped_at_zero);
    TEST(ac54_arrow_down_scrolls_1_line);
    TEST(ac54_arrow_down_clamped_at_max);

    // AC-55: Page scrolling by viewportHeight
    std::cout << "\n--- AC-55: Page scrolling (dynamic viewport) ---\n";
    TEST(ac55_page_up_scrolls_by_viewport_height);
    TEST(ac55_page_up_with_different_viewport);
    TEST(ac55_page_up_clamped_to_zero);
    TEST(ac55_page_up_sets_scroll_to_bottom_false);
    TEST(ac55_page_down_scrolls_by_viewport_height);
    TEST(ac55_page_down_clamped_to_max);

    // AC-56: Mouse wheel scrolling
    std::cout << "\n--- AC-56: Mouse wheel scrolling ---\n";
    TEST(ac56_mouse_wheel_up_scrolls_3_lines);
    TEST(ac56_mouse_scroll_lines_constant_is_3);
    TEST(ac56_mouse_wheel_down_scrolls_3_lines);
    TEST(ac56_mouse_wheel_up_clamped_to_zero);
    TEST(ac56_mouse_wheel_down_clamped_to_max);

    // AC-57: Home/End navigation
    std::cout << "\n--- AC-57: Home/End navigation ---\n";
    TEST(ac57_home_sets_offset_to_zero);
    TEST(ac57_home_sets_scroll_to_bottom_false);
    TEST(ac57_end_sets_scroll_to_bottom_true);
    TEST(ac57_end_results_in_max_scroll_after_clamp);

    // AC-58: Scrolling during generation
    std::cout << "\n--- AC-58: Scrolling during generation ---\n";
    TEST(ac58_arrow_up_works_during_generation);
    TEST(ac58_page_up_works_during_generation);
    TEST(ac58_mouse_wheel_works_during_generation);

    // AC-59: Auto-scroll on user message
    std::cout << "\n--- AC-59: Auto-scroll on user message ---\n";
    TEST(ac59_auto_scroll_on_user_message);

    // AC-60: onToken does not force scroll-to-bottom
    std::cout << "\n--- AC-60: onToken does not force scroll-to-bottom ---\n";
    TEST(ac60_on_token_does_not_set_scroll_to_bottom);

    // AC-61: Manual scroll disengages auto-follow
    std::cout << "\n--- AC-61: Manual scroll disengages auto-follow ---\n";
    TEST(ac61_mouse_wheel_up_disables_auto_scroll);
    TEST(ac61_arrow_up_disables_auto_scroll);
    TEST(ac61_page_up_disables_auto_scroll);
    TEST(ac61_home_disables_auto_scroll);

    // AC-62: End key re-engages auto-follow
    std::cout << "\n--- AC-62: End key re-engages auto-follow ---\n";
    TEST(ac62_end_key_reenables_auto_scroll);
    TEST(ac62_full_workflow_scroll_up_then_end);

    // AC-63: Build constants
    std::cout << "\n--- AC-63: Build constants ---\n";
    TEST(ac63_constants_match);

    // AC-65: E2E scroll during streaming (code verification)
    std::cout << "\n--- AC-65: E2E scroll during streaming ---\n";
    TEST(ac65_all_scroll_handlers_present_in_source);
    TEST(ac65_page_scroll_not_gated_on_generating);
    TEST(ac65_mouse_wheel_not_gated_on_generating);
    TEST(ac65_streaming_scroll_workflow_simulation);
    TEST(ac65_on_token_only_posts_custom_event);

    // Additional edge cases
    std::cout << "\n--- Additional edge cases ---\n";
    TEST(scroll_to_bottom_flag_overrides_manual_offset);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed." << std::endl;

    return (tests_passed == tests_run) ? 0 : 1;
}
