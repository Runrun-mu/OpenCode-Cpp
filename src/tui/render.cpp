#include "render.h"
#include "markdown.h"
#include <sstream>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace opencodecpp {

Element renderMarkdown(const std::string& content) {
    return renderMarkdownStyled(content);
}

Element renderMessage(const ChatMessage& msg) {
    if (msg.role == "user") {
        return hbox({
            text("You: ") | bold | color(Color::Green),
            renderMarkdown(msg.content) | color(Color::GreenLight),
        });
    } else if (msg.role == "assistant") {
        return hbox({
            text("AI: ") | bold | color(Color::Cyan),
            renderMarkdown(msg.content) | color(Color::White),
        });
    } else if (msg.role == "tool") {
        std::string status = msg.toolStatus == "running" ? "⟳" : "✓";
        return text("[Tool: " + msg.toolName + "] " + status + " " + msg.content)
            | color(Color::Yellow) | dim;
    }
    return text(msg.content);
}

Element renderStatusBar(
    const std::string& modelName,
    int inputTokens,
    int outputTokens,
    double cost
) {
    char costStr[32];
    snprintf(costStr, sizeof(costStr), "$%.4f", cost);

    return hbox({
        text(" Model: " + modelName) | bold,
        text(" │ "),
        text("Tokens: " + std::to_string(inputTokens) + "/" + std::to_string(outputTokens)),
        text(" │ "),
        text("Cost: " + std::string(costStr)),
        text(" "),
    }) | bgcolor(Color::GrayDark) | color(Color::White);
}

} // namespace opencodecpp
