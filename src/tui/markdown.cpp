#include "markdown.h"
#include <sstream>
#include <regex>

using namespace ftxui;

namespace opencodecpp {

std::vector<InlineSegment> MarkdownParser::parseInline(const std::string& text) {
    std::vector<InlineSegment> segments;
    size_t i = 0;
    std::string current;

    while (i < text.size()) {
        // Check for inline code: `code`
        if (text[i] == '`') {
            if (!current.empty()) {
                segments.push_back({InlineSegment::Style::Normal, current});
                current.clear();
            }
            size_t end = text.find('`', i + 1);
            if (end != std::string::npos) {
                segments.push_back({InlineSegment::Style::Code, text.substr(i + 1, end - i - 1)});
                i = end + 1;
                continue;
            }
            // No closing backtick - treat as normal text
            current += text[i];
            i++;
            continue;
        }

        // Check for bold: **text**
        if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
            if (!current.empty()) {
                segments.push_back({InlineSegment::Style::Normal, current});
                current.clear();
            }
            size_t end = text.find("**", i + 2);
            if (end != std::string::npos) {
                segments.push_back({InlineSegment::Style::Bold, text.substr(i + 2, end - i - 2)});
                i = end + 2;
                continue;
            }
            // No closing ** - treat as normal text
            current += text[i];
            i++;
            continue;
        }

        // Check for italic: *text*
        if (text[i] == '*') {
            if (!current.empty()) {
                segments.push_back({InlineSegment::Style::Normal, current});
                current.clear();
            }
            size_t end = text.find('*', i + 1);
            if (end != std::string::npos) {
                segments.push_back({InlineSegment::Style::Italic, text.substr(i + 1, end - i - 1)});
                i = end + 1;
                continue;
            }
            // No closing * - treat as normal
            current += text[i];
            i++;
            continue;
        }

        current += text[i];
        i++;
    }

    if (!current.empty()) {
        segments.push_back({InlineSegment::Style::Normal, current});
    }

    if (segments.empty()) {
        segments.push_back({InlineSegment::Style::Normal, ""});
    }

    return segments;
}

std::vector<ParsedLine> MarkdownParser::parse(const std::string& content) {
    std::vector<ParsedLine> result;
    std::istringstream stream(content);
    std::string line;
    bool in_code_block = false;
    std::string code_language;
    std::vector<std::string> code_lines;

    while (std::getline(stream, line)) {
        // Check for code block delimiter
        if (line.size() >= 3 && line.substr(0, 3) == "```") {
            if (in_code_block) {
                // Close code block
                ParsedLine pl;
                pl.type = ParsedLine::Type::CodeBlock;
                pl.language = code_language;
                pl.codeLines = code_lines;
                result.push_back(pl);
                code_lines.clear();
                code_language.clear();
                in_code_block = false;
            } else {
                // Open code block
                in_code_block = true;
                code_language = line.substr(3);
                // Trim whitespace from language
                size_t start = code_language.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    code_language = code_language.substr(start);
                } else {
                    code_language.clear();
                }
            }
            continue;
        }

        if (in_code_block) {
            code_lines.push_back(line);
            continue;
        }

        // Check for separator: ---, ***, ___
        if (line == "---" || line == "***" || line == "___") {
            ParsedLine pl;
            pl.type = ParsedLine::Type::Separator;
            result.push_back(pl);
            continue;
        }

        // Check for headings: # ## ###
        if (line.size() >= 2 && line[0] == '#') {
            int level = 0;
            size_t pos = 0;
            while (pos < line.size() && line[pos] == '#' && level < 3) {
                level++;
                pos++;
            }
            if (pos < line.size() && line[pos] == ' ') {
                ParsedLine pl;
                pl.type = ParsedLine::Type::Heading;
                pl.headingLevel = level;
                pl.rawText = line.substr(pos + 1);
                pl.segments = parseInline(pl.rawText);
                result.push_back(pl);
                continue;
            }
        }

        // Check for unordered list: - item
        if (line.size() >= 2 && line[0] == '-' && line[1] == ' ') {
            ParsedLine pl;
            pl.type = ParsedLine::Type::UnorderedList;
            pl.rawText = line.substr(2);
            pl.segments = parseInline(pl.rawText);
            result.push_back(pl);
            continue;
        }

        // Check for ordered list: 1. item
        if (line.size() >= 3) {
            size_t dotPos = line.find(". ");
            if (dotPos != std::string::npos && dotPos > 0 && dotPos <= 4) {
                bool allDigits = true;
                for (size_t j = 0; j < dotPos; j++) {
                    if (!isdigit(line[j])) {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits) {
                    ParsedLine pl;
                    pl.type = ParsedLine::Type::OrderedList;
                    pl.rawText = line;
                    pl.segments = parseInline(line.substr(dotPos + 2));
                    result.push_back(pl);
                    continue;
                }
            }
        }

        // Check for empty line
        if (line.empty()) {
            ParsedLine pl;
            pl.type = ParsedLine::Type::Empty;
            result.push_back(pl);
            continue;
        }

        // Regular text with inline formatting
        ParsedLine pl;
        pl.type = ParsedLine::Type::Text;
        pl.rawText = line;
        pl.segments = parseInline(line);
        result.push_back(pl);
    }

    // Handle unclosed code block
    if (in_code_block) {
        ParsedLine pl;
        pl.type = ParsedLine::Type::CodeBlock;
        pl.language = code_language;
        pl.codeLines = code_lines;
        result.push_back(pl);
    }

    return result;
}

// Render inline segments as FTXUI Elements
static Element renderInlineSegments(const std::vector<InlineSegment>& segments) {
    std::vector<Element> elements;
    for (auto& seg : segments) {
        switch (seg.style) {
            case InlineSegment::Style::Bold:
                elements.push_back(text(seg.text) | bold);
                break;
            case InlineSegment::Style::Italic:
                elements.push_back(text(seg.text) | dim);
                break;
            case InlineSegment::Style::Code:
                elements.push_back(text(seg.text) | bgcolor(Color::GrayDark));
                break;
            case InlineSegment::Style::Normal:
            default:
                elements.push_back(text(seg.text));
                break;
        }
    }
    if (elements.empty()) return text("");
    return hbox(elements);
}

Element renderMarkdownStyled(const std::string& content) {
    MarkdownParser parser;
    auto lines = parser.parse(content);

    std::vector<Element> elements;

    for (auto& line : lines) {
        switch (line.type) {
            case ParsedLine::Type::Heading: {
                auto headingEl = renderInlineSegments(line.segments) | bold;
                switch (line.headingLevel) {
                    case 1:
                        headingEl = headingEl | color(Color::CyanLight);
                        break;
                    case 2:
                        headingEl = headingEl | color(Color::Cyan);
                        break;
                    case 3:
                    default:
                        headingEl = headingEl | color(Color::GrayLight);
                        break;
                }
                elements.push_back(headingEl);
                break;
            }

            case ParsedLine::Type::CodeBlock: {
                std::vector<Element> codeElements;
                if (!line.language.empty()) {
                    codeElements.push_back(text(" " + line.language + " ") | bold | color(Color::Yellow));
                }
                for (auto& cl : line.codeLines) {
                    codeElements.push_back(text(cl) | color(Color::Green));
                }
                if (codeElements.empty()) {
                    codeElements.push_back(text(""));
                }
                elements.push_back(
                    vbox(codeElements) | bgcolor(Color::GrayDark) | border
                );
                break;
            }

            case ParsedLine::Type::UnorderedList:
                elements.push_back(
                    hbox({text("  • "), renderInlineSegments(line.segments)})
                );
                break;

            case ParsedLine::Type::OrderedList:
                elements.push_back(
                    hbox({text("  "), renderInlineSegments(line.segments)})
                );
                break;

            case ParsedLine::Type::Separator:
                elements.push_back(separator());
                break;

            case ParsedLine::Type::Empty:
                elements.push_back(text(""));
                break;

            case ParsedLine::Type::Text:
            default:
                elements.push_back(renderInlineSegments(line.segments));
                break;
        }
    }

    if (elements.empty()) {
        elements.push_back(text(""));
    }

    return vbox(elements);
}

} // namespace opencodecpp
