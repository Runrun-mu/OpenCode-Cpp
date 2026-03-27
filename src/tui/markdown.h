#pragma once
#include <string>
#include <vector>
#include <ftxui/dom/elements.hpp>

namespace opencodecpp {

// Inline text segment with style info
struct InlineSegment {
    enum class Style {
        Normal,
        Bold,
        Italic,
        Code
    };
    Style style = Style::Normal;
    std::string text;
};

// Parsed line with type info
struct ParsedLine {
    enum class Type {
        Text,
        Heading,
        CodeBlock,
        UnorderedList,
        OrderedList,
        Separator,
        Empty
    };
    Type type = Type::Text;
    int headingLevel = 0;
    std::string language;  // for code blocks
    std::vector<std::string> codeLines;  // for code blocks
    std::vector<InlineSegment> segments;  // for text/heading/list
    std::string rawText;  // original text content
};

class MarkdownParser {
public:
    // Parse markdown content into structured lines
    std::vector<ParsedLine> parse(const std::string& content);

private:
    // Parse inline formatting (bold, italic, code)
    std::vector<InlineSegment> parseInline(const std::string& text);
};

// Render markdown content as FTXUI Element (main entry point)
ftxui::Element renderMarkdownStyled(const std::string& content);

} // namespace opencodecpp
