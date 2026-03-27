#pragma once
#include <string>
#include <vector>
#include <ftxui/dom/elements.hpp>

namespace opencodecpp {

struct ChatMessage {
    std::string role; // "user", "assistant", "tool"
    std::string content;
    std::string toolName; // For tool status messages
    std::string toolStatus; // "running", "done"
};

// Render a chat message as FTXUI elements
ftxui::Element renderMessage(const ChatMessage& msg);

// Render the status bar
ftxui::Element renderStatusBar(
    const std::string& modelName,
    int inputTokens,
    int outputTokens,
    double cost
);

// Parse content for code blocks and render with syntax highlighting
ftxui::Element renderMarkdown(const std::string& content);

} // namespace opencodecpp
