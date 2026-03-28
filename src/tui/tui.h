#pragma once
#include "render.h"
#include "input.h"
#include "../config/config.h"
#include "../llm/agent_loop.h"
#include "../session/session.h"
#include "../skills/skill.h"
#include "../skills/skill_tool.h"
#include <memory>
#include <atomic>
#include <string>
#include <vector>

namespace opencodecpp {

struct SlashCommand {
    std::string command;
    std::string description;
};

class TUI {
public:
    TUI(Config& config);
    ~TUI();

    void run();

private:
    void setupTools();
    void handleInput(const std::string& input);
    bool handleSlashCommand(const std::string& input, std::mutex& chatMutex);
    double calculateCost() const;

    Config& config_;
    SessionManager session_;
    std::shared_ptr<LLMProvider> provider_;
    std::unique_ptr<AgentLoop> agentLoop_;
    InputHandler inputHandler_;
    SkillManager skillManager_;

    std::vector<ChatMessage> chatMessages_;
    std::atomic<bool> isGenerating_{false};
    std::atomic<bool> cancelGeneration_{false};

    int totalInputTokens_ = 0;
    int totalOutputTokens_ = 0;

    // Slash command auto-suggestion (F-4)
    std::vector<SlashCommand> slashCommands_ = {
        {"/compact", "Compact conversation history"},
        {"/status", "Show session status"},
        {"/skill", "Manage skills"},
        {"/clear", "Clear chat display"},
        {"/help", "Show available commands"}
    };
    int suggestionIndex_ = 0;
    bool showSuggestions_ = false;

    // Price per 1M tokens
    static constexpr double CLAUDE_INPUT_PRICE = 3.0;
    static constexpr double CLAUDE_OUTPUT_PRICE = 15.0;
    static constexpr double GPT4O_INPUT_PRICE = 2.5;
    static constexpr double GPT4O_OUTPUT_PRICE = 10.0;
};

} // namespace opencodecpp
