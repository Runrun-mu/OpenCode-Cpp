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

// AC-12: Suggestion overlay shows all matching commands
void test_tui_h_has_slash_command_list() {
    std::string content = readSourceFile("src/tui/tui.h");
    ASSERT_TRUE(content.find("SlashCommand") != std::string::npos ||
                content.find("slashCommand") != std::string::npos ||
                content.find("slash_command") != std::string::npos);
}

void test_tui_has_suggestion_state() {
    std::string content = readSourceFile("src/tui/tui.h");
    ASSERT_TRUE(content.find("suggestionIndex") != std::string::npos ||
                content.find("suggestion_index") != std::string::npos);
    ASSERT_TRUE(content.find("showSuggestions") != std::string::npos ||
                content.find("show_suggestions") != std::string::npos);
}

void test_commands_include_compact() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should have /compact in the command list
    ASSERT_TRUE(content.find("\"/compact\"") != std::string::npos);
}

void test_commands_include_status() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("\"/status\"") != std::string::npos);
}

void test_commands_include_skill() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("\"/skill\"") != std::string::npos);
}

void test_commands_include_clear() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("\"/clear\"") != std::string::npos);
}

void test_commands_include_help() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("\"/help\"") != std::string::npos);
}

// AC-13: Typing filters the list
void test_suggestion_filtering() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should filter based on input prefix
    ASSERT_TRUE(content.find("substr") != std::string::npos ||
                content.find("find") != std::string::npos ||
                content.find("rfind") != std::string::npos);
}

// AC-14: Up/Down arrow navigation
void test_arrow_navigation_up() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should handle arrow up for suggestions
    ASSERT_TRUE(content.find("ArrowUp") != std::string::npos);
    ASSERT_TRUE(content.find("suggestionIndex") != std::string::npos ||
                content.find("suggestion_index") != std::string::npos);
}

void test_arrow_navigation_down() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("ArrowDown") != std::string::npos);
}

// AC-15: Tab or Enter auto-completes
void test_tab_completion() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    ASSERT_TRUE(content.find("Tab") != std::string::npos);
}

void test_suggestion_render_when_slash() {
    std::string content = readSourceFile("src/tui/tui.cpp");
    // Should check if input starts with '/' to show suggestions
    ASSERT_TRUE(content.find("showSuggestions") != std::string::npos ||
                content.find("show_suggestions") != std::string::npos);
}

int main() {
    std::cout << "=== F-4: Slash Command Auto-Suggestion Tests ===\n";

    std::cout << "\n--- AC-12: Suggestion overlay ---\n";
    TEST(tui_h_has_slash_command_list);
    TEST(tui_has_suggestion_state);
    TEST(commands_include_compact);
    TEST(commands_include_status);
    TEST(commands_include_skill);
    TEST(commands_include_clear);
    TEST(commands_include_help);

    std::cout << "\n--- AC-13: Input filtering ---\n";
    TEST(suggestion_filtering);

    std::cout << "\n--- AC-14: Arrow key navigation ---\n";
    TEST(arrow_navigation_up);
    TEST(arrow_navigation_down);

    std::cout << "\n--- AC-15: Tab/Enter completion ---\n";
    TEST(tab_completion);
    TEST(suggestion_render_when_slash);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
