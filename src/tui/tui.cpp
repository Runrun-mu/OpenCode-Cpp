#include "tui.h"
#include "../llm/anthropic.h"
#include "../llm/openai.h"
#include "../tools/bash.h"
#include "../tools/file_read.h"
#include "../tools/file_write.h"
#include "../tools/file_edit.h"
#include "../tools/glob.h"
#include "../tools/grep.h"
#include "../tools/ls.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <thread>
#include <mutex>
#include <climits>
#include <atomic>
#include <unistd.h>
#include <algorithm>

using namespace ftxui;

namespace opencodecpp {

TUI::TUI(Config& config) : config_(config) {
    // Initialize session
    session_.initialize(Config::dbFilePath());

    // Create or load session
    if (!config_.session_id.empty()) {
        if (!session_.loadSession(config_.session_id)) {
            // Session not found, create new one
            session_.createSession();
        }
    } else {
        session_.createSession();
    }

    // Create LLM provider
    std::string provider = config_.getProvider();
    if (provider == "openai") {
        provider_ = std::make_shared<OpenAIProvider>(
            config_.openai_api_key, config_.getModel(), config_.openai_base_url
        );
    } else {
        provider_ = std::make_shared<AnthropicProvider>(
            config_.anthropic_api_key, config_.getModel()
        );
    }

    agentLoop_ = std::make_unique<AgentLoop>(provider_, session_);
    setupTools();

    // Discover and auto-activate skills
    skillManager_.discover();
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        skillManager_.autoActivate(std::string(cwd));
    }

    // Log auto-activated skills
    for (auto& skill : skillManager_.getAllSkills()) {
        if (skill.active) {
            ChatMessage sysMsg;
            sysMsg.role = "tool";
            sysMsg.toolName = "skill";
            sysMsg.toolStatus = "done";
            sysMsg.content = "Auto-activated skill: " + skill.name;
            chatMessages_.push_back(sysMsg);
        }
    }
}

TUI::~TUI() {}

void TUI::setupTools() {
    agentLoop_->registerTool(std::make_shared<BashTool>());
    agentLoop_->registerTool(std::make_shared<FileReadTool>());
    agentLoop_->registerTool(std::make_shared<FileWriteTool>());
    agentLoop_->registerTool(std::make_shared<FileEditTool>());
    agentLoop_->registerTool(std::make_shared<GlobTool>());
    agentLoop_->registerTool(std::make_shared<GrepTool>());
    agentLoop_->registerTool(std::make_shared<LsTool>());
    agentLoop_->registerTool(std::make_shared<SkillTool>(skillManager_));
}

double TUI::calculateCost() const {
    std::string model = config_.getModel();
    double inputPrice, outputPrice;
    if (model.find("gpt") != std::string::npos) {
        inputPrice = GPT4O_INPUT_PRICE;
        outputPrice = GPT4O_OUTPUT_PRICE;
    } else {
        inputPrice = CLAUDE_INPUT_PRICE;
        outputPrice = CLAUDE_OUTPUT_PRICE;
    }
    return (totalInputTokens_ * inputPrice + totalOutputTokens_ * outputPrice) / 1000000.0;
}

void TUI::handleInput(const std::string& input) {
    if (input.empty()) return;

    inputHandler_.addToHistory(input);

    // Add user message to display
    ChatMessage userMsg;
    userMsg.role = "user";
    userMsg.content = input;
    chatMessages_.push_back(userMsg);

    // Start AI response
    ChatMessage aiMsg;
    aiMsg.role = "assistant";
    aiMsg.content = "";
    chatMessages_.push_back(aiMsg);

    isGenerating_ = true;
    cancelGeneration_ = false;
}

bool TUI::handleSlashCommand(const std::string& input, std::mutex& chatMutex) {
    // Handle /status command (AC-3: includes context_window and compact_threshold)
    if (input.substr(0, 7) == "/status") {
        ChatMessage sysMsg;
        sysMsg.role = "tool";
        sysMsg.toolName = "status";
        sysMsg.toolStatus = "done";
        sysMsg.content = "Model: " + config_.getModel() + "\n"
            + "Session: " + session_.currentSessionId() + "\n"
            + "Messages: " + std::to_string(session_.getHistory().size()) + "\n"
            + "Tokens (in/out): " + std::to_string(totalInputTokens_) + "/" + std::to_string(totalOutputTokens_) + "\n"
            + "Cost: $" + std::to_string(calculateCost()) + "\n"
            + "Context window: " + std::to_string(config_.context_window) + "\n"
            + "Compact threshold: " + std::to_string(config_.compact_threshold) + "\n"
            + "Estimated tokens: " + std::to_string(static_cast<int>(session_.estimateTokens()));
        chatMessages_.push_back(sysMsg);
        return true;
    }

    // Handle /compact command
    if (input.substr(0, 8) == "/compact") {
        ChatMessage startMsg;
        startMsg.role = "tool";
        startMsg.toolName = "compact";
        startMsg.toolStatus = "running";
        startMsg.content = "Compacting conversation...";
        chatMessages_.push_back(startMsg);
        return true; // Will be handled asynchronously
    }

    // Handle /clear command
    if (input.substr(0, 6) == "/clear") {
        chatMessages_.clear();
        return true;
    }

    // Handle /help command
    if (input.substr(0, 5) == "/help") {
        ChatMessage sysMsg;
        sysMsg.role = "tool";
        sysMsg.toolName = "help";
        sysMsg.toolStatus = "done";
        std::string content = "Available commands:\n";
        for (auto& cmd : slashCommands_) {
            content += "  " + cmd.command + " - " + cmd.description + "\n";
        }
        sysMsg.content = content;
        chatMessages_.push_back(sysMsg);
        return true;
    }

    // Handle /skill command
    if (input.substr(0, 6) != "/skill") return false;

    std::string rest = input.size() > 6 ? input.substr(7) : "";
    // Trim
    size_t start = rest.find_first_not_of(" \t");
    if (start != std::string::npos) rest = rest.substr(start);
    else rest.clear();

    ChatMessage sysMsg;
    sysMsg.role = "tool";
    sysMsg.toolName = "skill";
    sysMsg.toolStatus = "done";

    if (rest.empty() || rest == "list") {
        // /skill list - show all skills
        std::string content = "Available skills:\n";
        auto& skills = skillManager_.getAllSkills();
        if (skills.empty()) {
            content += "  (none found)\n";
        }
        for (auto& skill : skills) {
            content += "  " + skill.name + " - " + skill.description;
            if (skill.active) content += " [ACTIVE]";
            content += "\n";
        }
        sysMsg.content = content;
    } else {
        // /skill <name> - activate a skill
        if (skillManager_.activate(rest)) {
            sysMsg.content = "Activated skill: " + rest;
        } else {
            sysMsg.content = "Error: Skill not found: " + rest;
        }
    }

    chatMessages_.push_back(sysMsg);
    return true;
}

void TUI::run() {
    auto screen = ScreenInteractive::Fullscreen();

    std::string inputText;
    std::mutex chatMutex;
    std::atomic<bool> scrollToBottom{true};

    // Input component
    auto inputOption = InputOption();
    inputOption.multiline = false;
    auto inputComponent = Input(&inputText, "Type your message...", inputOption);

    // Main renderer
    auto renderer = Renderer(inputComponent, [&] {
        std::lock_guard<std::mutex> lock(chatMutex);

        // Chat history
        std::vector<Element> chatElements;
        for (auto& msg : chatMessages_) {
            chatElements.push_back(renderMessage(msg));
            chatElements.push_back(separator());
        }

        if (chatElements.empty()) {
            chatElements.push_back(
                text("Welcome to OpenCodeCpp! Type a message to begin.") | color(Color::Cyan) | center
            );
        }

        // F-1: FTXUI native scrolling - focus last element for auto-scroll
        if (scrollToBottom.load() && !chatElements.empty()) {
            chatElements.back() = chatElements.back() | focus;
        }
        auto chatBox = vbox(chatElements) | vscroll_indicator | yframe | flex;

        // Input area
        auto inputArea = hbox({
            text("> ") | bold | color(Color::Green),
            inputComponent->Render() | flex,
        }) | border;

        // F-4: Slash command suggestion overlay (AC-12, AC-13)
        Elements suggestionElements;
        if (showSuggestions_ && !inputText.empty() && inputText[0] == '/') {
            // Filter commands based on input
            std::vector<SlashCommand> filtered;
            for (auto& cmd : slashCommands_) {
                if (cmd.command.find(inputText) == 0 || inputText == "/") {
                    filtered.push_back(cmd);
                }
            }

            if (!filtered.empty()) {
                // Clamp suggestionIndex_
                if (suggestionIndex_ >= static_cast<int>(filtered.size())) {
                    suggestionIndex_ = static_cast<int>(filtered.size()) - 1;
                }
                if (suggestionIndex_ < 0) suggestionIndex_ = 0;

                for (int i = 0; i < static_cast<int>(filtered.size()); i++) {
                    auto item = hbox({
                        text(filtered[i].command) | bold,
                        text("  " + filtered[i].description) | dim,
                    });
                    if (i == suggestionIndex_) {
                        item = item | inverted;
                    }
                    suggestionElements.push_back(item);
                }
            }
        }

        // Status bar
        auto statusBar = renderStatusBar(
            config_.getModel(),
            totalInputTokens_,
            totalOutputTokens_,
            calculateCost()
        );

        Elements layout;
        layout.push_back(chatBox | flex);
        layout.push_back(separator());
        if (!suggestionElements.empty()) {
            layout.push_back(vbox(suggestionElements) | border | color(Color::Yellow));
        }
        layout.push_back(inputArea);
        layout.push_back(statusBar);

        return vbox(layout);
    });

    // Event handler
    auto component = CatchEvent(renderer, [&](Event event) {
        // Ctrl+C handling
        if (event == Event::Special("\x03")) {
            if (isGenerating_) {
                cancelGeneration_ = true;
                return true;
            }
            screen.Exit();
            return true;
        }

        // Ctrl+L - clear display
        if (event == Event::Special("\x0C")) {
            std::lock_guard<std::mutex> lock(chatMutex);
            chatMessages_.clear();
            return true;
        }

        // F-4: Update suggestion state when input changes
        {
            std::lock_guard<std::mutex> lock(chatMutex);
            if (!inputText.empty() && inputText[0] == '/') {
                showSuggestions_ = true;
            } else {
                showSuggestions_ = false;
                suggestionIndex_ = 0;
            }
        }

        // F-4: Tab key - auto-complete selected suggestion (AC-15)
        if (event == Event::Tab && showSuggestions_) {
            std::lock_guard<std::mutex> lock(chatMutex);
            // Filter commands
            std::vector<SlashCommand> filtered;
            for (auto& cmd : slashCommands_) {
                if (cmd.command.find(inputText) == 0 || inputText == "/") {
                    filtered.push_back(cmd);
                }
            }
            if (!filtered.empty() && suggestionIndex_ < static_cast<int>(filtered.size())) {
                inputText = filtered[suggestionIndex_].command;
                showSuggestions_ = false;
                suggestionIndex_ = 0;
            }
            return true;
        }

        // Enter key - submit input
        if (event == Event::Return) {
            // F-4: If suggestions are visible, auto-complete instead of submitting (AC-15)
            if (showSuggestions_ && !inputText.empty() && inputText[0] == '/') {
                std::lock_guard<std::mutex> lock(chatMutex);
                std::vector<SlashCommand> filtered;
                for (auto& cmd : slashCommands_) {
                    if (cmd.command.find(inputText) == 0 || inputText == "/") {
                        filtered.push_back(cmd);
                    }
                }
                // If input is an exact command match, submit it; otherwise autocomplete
                bool exactMatch = false;
                for (auto& cmd : slashCommands_) {
                    if (cmd.command == inputText) { exactMatch = true; break; }
                }
                if (!exactMatch && !filtered.empty() && suggestionIndex_ < static_cast<int>(filtered.size())) {
                    inputText = filtered[suggestionIndex_].command;
                    showSuggestions_ = false;
                    suggestionIndex_ = 0;
                    return true;
                }
            }

            if (!inputText.empty() && !isGenerating_) {
                std::string msg = inputText;
                inputText.clear();
                showSuggestions_ = false;
                suggestionIndex_ = 0;

                {
                    std::lock_guard<std::mutex> lock(chatMutex);

                    // Check for slash commands (/skill, /status, /compact, /clear, /help)
                    if (msg[0] == '/' && handleSlashCommand(msg, chatMutex)) {
                        // /compact needs async handling
                        if (msg.substr(0, 8) == "/compact") {
                            // Will run compact in background thread below
                        } else {
                            scrollToBottom.store(true);
                            return true;
                        }
                    }

                    // If /compact, run in background
                    if (msg.substr(0, 8) == "/compact") {
                        scrollToBottom.store(true);
                        std::thread([this, &chatMutex, &screen, &scrollToBottom]() {
                            std::string systemPrompt =
                                "You are an AI coding assistant.";
                            auto result = agentLoop_->compact(systemPrompt);
                            {
                                std::lock_guard<std::mutex> lock(chatMutex);
                                totalInputTokens_ = agentLoop_->totalInputTokens();
                                totalOutputTokens_ = agentLoop_->totalOutputTokens();

                                ChatMessage doneMsg;
                                doneMsg.role = "tool";
                                doneMsg.toolName = "compact";
                                doneMsg.toolStatus = "done";
                                if (!result.success) {
                                    doneMsg.content = "Compact failed: " + result.error;
                                } else {
                                    doneMsg.content = result.statusMessage + "\n\nSummary:\n" + result.summary;
                                }
                                chatMessages_.push_back(doneMsg);
                            }
                            screen.Post(Event::Custom);
                        }).detach();
                        return true;
                    }

                    handleInput(msg);
                    scrollToBottom.store(true); // Auto-scroll on user message
                }

                // Run agent in background thread
                std::thread([this, msg, &chatMutex, &screen, &scrollToBottom]() {
                    std::string systemPrompt =
                        "You are an AI coding assistant. Help the user with their programming tasks. "
                        "You have access to tools for reading, writing, and editing files, "
                        "running shell commands, and searching the codebase.";

                    if (!config_.custom_instructions.empty()) {
                        systemPrompt += "\n\nCustom instructions: " + config_.custom_instructions;
                    }

                    // Inject active skill content into system prompt
                    std::string skillPrompt = skillManager_.getActiveSkillPrompt();
                    if (!skillPrompt.empty()) {
                        systemPrompt += skillPrompt;
                    }

                    AgentCallbacks callbacks;
                    callbacks.onToken = [this, &chatMutex, &screen, &scrollToBottom](const std::string& token) {
                        std::lock_guard<std::mutex> lock(chatMutex);
                        if (!chatMessages_.empty() && chatMessages_.back().role == "assistant") {
                            chatMessages_.back().content += token;
                        }
                        // Only auto-scroll if user hasn't manually scrolled away
                        // (scrollToBottom is already true means user is following the stream)
                        screen.Post(Event::Custom);
                    };
                    callbacks.onToolStatus = [this, &chatMutex, &screen, &scrollToBottom](const std::string& name, const std::string& status) {
                        std::lock_guard<std::mutex> lock(chatMutex);
                        ChatMessage toolMsg;
                        toolMsg.role = "tool";
                        toolMsg.toolName = name;
                        toolMsg.toolStatus = status;
                        // Insert before the last assistant message
                        if (chatMessages_.size() >= 2) {
                            chatMessages_.insert(chatMessages_.end() - 1, toolMsg);
                        }
                        screen.Post(Event::Custom);
                    };
                    callbacks.cancelCheck = [this]() -> bool {
                        return cancelGeneration_.load();
                    };

                    auto resp = agentLoop_->run(msg, systemPrompt, callbacks);

                    {
                        std::lock_guard<std::mutex> lock(chatMutex);
                        totalInputTokens_ = agentLoop_->totalInputTokens();
                        totalOutputTokens_ = agentLoop_->totalOutputTokens();
                        isGenerating_ = false;

                        if (!resp.error.empty()) {
                            ChatMessage errMsg;
                            errMsg.role = "assistant";
                            errMsg.content = "Error: " + resp.error;
                            chatMessages_.push_back(errMsg);
                        }

                        // AC-11: Auto-compaction when estimateTokens() > compact_threshold OR messages > 50
                        if (session_.estimateTokens() > config_.compact_threshold ||
                            session_.getHistory().size() > 50) {
                            auto result = agentLoop_->compact(systemPrompt);
                            if (result.success) {
                                ChatMessage compactMsg;
                                compactMsg.role = "tool";
                                compactMsg.toolName = "compact";
                                compactMsg.toolStatus = "done";
                                compactMsg.content = "Auto-compact triggered. " + result.statusMessage;
                                chatMessages_.push_back(compactMsg);
                                totalInputTokens_ = agentLoop_->totalInputTokens();
                                totalOutputTokens_ = agentLoop_->totalOutputTokens();
                            }
                        }
                    }
                    screen.Post(Event::Custom);
                }).detach();
            }
            return true;
        }

        // Page Up - scroll up (AC-3/AC-5: FTXUI handles natively via yframe)
        if (event == Event::PageUp) {
            scrollToBottom.store(false);
            return false; // Let FTXUI handle the actual scrolling
        }

        // Page Down - scroll down (AC-5)
        if (event == Event::PageDown) {
            return false; // Let FTXUI handle the actual scrolling
        }

        // Home - jump to top (AC-3)
        if (event == Event::Home) {
            scrollToBottom.store(false);
            return false; // Let FTXUI handle
        }

        // End - jump to bottom (AC-4)
        if (event == Event::End) {
            scrollToBottom.store(true);
            return true;
        }

        // Mouse wheel scroll (AC-3)
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                scrollToBottom.store(false);
                return false; // Let FTXUI handle
            }
            if (event.mouse().button == Mouse::WheelDown) {
                return false; // Let FTXUI handle
            }
        }

        // F-4: Up arrow - navigate suggestions when visible (AC-14)
        if (event == Event::ArrowUp) {
            if (showSuggestions_) {
                std::lock_guard<std::mutex> lock(chatMutex);
                if (suggestionIndex_ > 0) suggestionIndex_--;
                return true;
            }
            if (inputText.empty() || isGenerating_) {
                scrollToBottom.store(false);
                return false; // Let FTXUI handle
            }
            std::string prev = inputHandler_.getPreviousInput();
            if (!prev.empty()) inputText = prev;
            return true;
        }

        // F-4: Down arrow - navigate suggestions when visible (AC-14)
        if (event == Event::ArrowDown) {
            if (showSuggestions_) {
                std::lock_guard<std::mutex> lock(chatMutex);
                // Filter to find max index
                int maxIdx = 0;
                for (auto& cmd : slashCommands_) {
                    if (cmd.command.find(inputText) == 0 || inputText == "/") {
                        maxIdx++;
                    }
                }
                if (suggestionIndex_ < maxIdx - 1) suggestionIndex_++;
                return true;
            }
            if (inputText.empty() || isGenerating_) {
                return false; // Let FTXUI handle
            }
            inputText = inputHandler_.getNextInput();
            return true;
        }

        return false;
    });

    screen.Loop(component);
}

} // namespace opencodecpp
