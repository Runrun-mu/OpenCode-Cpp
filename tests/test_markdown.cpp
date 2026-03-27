#include <iostream>
#include <string>
#include "tui/markdown.h"

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
#define ASSERT_FALSE(x) if (x) throw std::runtime_error(std::string("Assertion failed: !") + #x)
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b)

// Helper: count elements returned by renderMarkdown (we test that parsing produces elements without crash)
// Since FTXUI Elements are opaque, we test the MarkdownParser directly for state logic.

// === AC-100: Code block rendering ===
void test_code_block_basic() {
    MarkdownParser parser;
    auto result = parser.parse("```python\ndef hello():\n    print('world')\n```");
    // Should not crash
    ASSERT_TRUE(result.size() > 0);
}

void test_code_block_language_capture() {
    MarkdownParser parser;
    auto result = parser.parse("```cpp\nint main() {}\n```");
    // Parser should detect code block with language
    ASSERT_TRUE(result.size() > 0);
    // Check first element is a code block
    bool foundCodeBlock = false;
    for (auto& line : result) {
        if (line.type == ParsedLine::Type::CodeBlock) {
            foundCodeBlock = true;
            ASSERT_EQ(line.language, std::string("cpp"));
        }
    }
    ASSERT_TRUE(foundCodeBlock);
}

// === AC-101: Inline code rendering ===
void test_inline_code() {
    MarkdownParser parser;
    auto result = parser.parse("Use `printf` to print");
    ASSERT_TRUE(result.size() > 0);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::Text);
    // Check inline segments contain code segment
    bool foundInlineCode = false;
    for (auto& seg : result[0].segments) {
        if (seg.style == InlineSegment::Style::Code) {
            foundInlineCode = true;
            ASSERT_EQ(seg.text, std::string("printf"));
        }
    }
    ASSERT_TRUE(foundInlineCode);
}

// === AC-102: Heading rendering ===
void test_heading_h1() {
    MarkdownParser parser;
    auto result = parser.parse("# Hello World");
    ASSERT_TRUE(result.size() >= 1);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::Heading);
    ASSERT_EQ(result[0].headingLevel, 1);
}

void test_heading_h2() {
    MarkdownParser parser;
    auto result = parser.parse("## Section");
    ASSERT_TRUE(result.size() >= 1);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::Heading);
    ASSERT_EQ(result[0].headingLevel, 2);
}

void test_heading_h3() {
    MarkdownParser parser;
    auto result = parser.parse("### Subsection");
    ASSERT_TRUE(result.size() >= 1);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::Heading);
    ASSERT_EQ(result[0].headingLevel, 3);
}

// === AC-103: Bold/Italic ===
void test_bold() {
    MarkdownParser parser;
    auto result = parser.parse("This is **bold** text");
    ASSERT_TRUE(result.size() >= 1);
    bool foundBold = false;
    for (auto& seg : result[0].segments) {
        if (seg.style == InlineSegment::Style::Bold) {
            foundBold = true;
            ASSERT_EQ(seg.text, std::string("bold"));
        }
    }
    ASSERT_TRUE(foundBold);
}

void test_italic() {
    MarkdownParser parser;
    auto result = parser.parse("This is *italic* text");
    ASSERT_TRUE(result.size() >= 1);
    bool foundItalic = false;
    for (auto& seg : result[0].segments) {
        if (seg.style == InlineSegment::Style::Italic) {
            foundItalic = true;
            ASSERT_EQ(seg.text, std::string("italic"));
        }
    }
    ASSERT_TRUE(foundItalic);
}

// === AC-104: List rendering ===
void test_unordered_list() {
    MarkdownParser parser;
    auto result = parser.parse("- item one\n- item two");
    ASSERT_TRUE(result.size() >= 2);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::UnorderedList);
    ASSERT_TRUE(result[1].type == ParsedLine::Type::UnorderedList);
}

void test_ordered_list() {
    MarkdownParser parser;
    auto result = parser.parse("1. first\n2. second");
    ASSERT_TRUE(result.size() >= 2);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::OrderedList);
    ASSERT_TRUE(result[1].type == ParsedLine::Type::OrderedList);
}

// === AC-105: Separator rendering ===
void test_separator_dashes() {
    MarkdownParser parser;
    auto result = parser.parse("---");
    ASSERT_TRUE(result.size() >= 1);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::Separator);
}

void test_separator_asterisks() {
    MarkdownParser parser;
    auto result = parser.parse("***");
    ASSERT_TRUE(result.size() >= 1);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::Separator);
}

void test_separator_underscores() {
    MarkdownParser parser;
    auto result = parser.parse("___");
    ASSERT_TRUE(result.size() >= 1);
    ASSERT_TRUE(result[0].type == ParsedLine::Type::Separator);
}

// === AC-106: Streaming safety ===
void test_unclosed_code_block() {
    MarkdownParser parser;
    // Only opening ```, no closing
    auto result = parser.parse("```python\ndef hello():\n    pass");
    // Should not crash, should produce code block
    ASSERT_TRUE(result.size() > 0);
    bool foundCodeBlock = false;
    for (auto& line : result) {
        if (line.type == ParsedLine::Type::CodeBlock) {
            foundCodeBlock = true;
        }
    }
    ASSERT_TRUE(foundCodeBlock);
}

void test_partial_bold() {
    MarkdownParser parser;
    // Incomplete bold markers - should not crash
    auto result = parser.parse("This is **partial");
    ASSERT_TRUE(result.size() > 0);
}

void test_empty_content() {
    MarkdownParser parser;
    auto result = parser.parse("");
    // Should not crash, may return empty or single empty line
    ASSERT_TRUE(true);
}

// === AC-107: Code block state tracking ===
void test_code_block_state_tracking() {
    MarkdownParser parser;
    auto result = parser.parse("Before\n```\ncode line 1\ncode line 2\n```\nAfter");
    // Should have: text, codeblock, text
    bool foundText = false;
    bool foundCode = false;
    for (auto& line : result) {
        if (line.type == ParsedLine::Type::Text) foundText = true;
        if (line.type == ParsedLine::Type::CodeBlock) foundCode = true;
    }
    ASSERT_TRUE(foundText);
    ASSERT_TRUE(foundCode);
}

void test_code_block_resume_normal_after_close() {
    MarkdownParser parser;
    auto result = parser.parse("```\ncode\n```\n# Heading After");
    // After code block closes, should parse heading
    bool foundHeading = false;
    for (auto& line : result) {
        if (line.type == ParsedLine::Type::Heading) foundHeading = true;
    }
    ASSERT_TRUE(foundHeading);
}

// === AC-108: renderMarkdown integration ===
void test_render_markdown_returns_element() {
    // Just test that calling renderMarkdown doesn't crash
    auto elem = renderMarkdownStyled("# Hello\n\nSome text\n```\ncode\n```");
    ASSERT_TRUE(true); // If we get here, no crash
}

// === AC-110: Streaming real-time (incremental content) ===
void test_incremental_content() {
    MarkdownParser parser;
    // Simulate streaming: first few tokens
    auto r1 = parser.parse("# Hel");
    ASSERT_TRUE(r1.size() > 0);

    // More tokens arrive
    auto r2 = parser.parse("# Hello World\n\nSome text");
    ASSERT_TRUE(r2.size() > 0);
}

int main() {
    std::cout << "=== Markdown Renderer Tests ===\n";

    TEST(code_block_basic);
    TEST(code_block_language_capture);
    TEST(inline_code);
    TEST(heading_h1);
    TEST(heading_h2);
    TEST(heading_h3);
    TEST(bold);
    TEST(italic);
    TEST(unordered_list);
    TEST(ordered_list);
    TEST(separator_dashes);
    TEST(separator_asterisks);
    TEST(separator_underscores);
    TEST(unclosed_code_block);
    TEST(partial_bold);
    TEST(empty_content);
    TEST(code_block_state_tracking);
    TEST(code_block_resume_normal_after_close);
    TEST(render_markdown_returns_element);
    TEST(incremental_content);

    std::cout << "\nResults: " << tests_passed << "/" << tests_run << " passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
