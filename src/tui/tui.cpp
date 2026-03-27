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
    std::atomic<int> scrollOffset{0};
    std::atomic<bool> scrollToBottom{true};
    std::atomic<int> viewportHeight{20}; // dynamic, updated each render frame
    constexpr int MOUSE_SCROLL_LINES = 3;
    constexpr int UI_CHROME_HEIGHT = 5; // input border(3) + status bar(1) + separator(1)

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

        // AC-53: Compute dynamic viewport height from terminal size
        int termHeight = ftxui::Terminal::Size().dimy;
        int vpHeight = std::max(5, termHeight - UI_CHROME_HEIGHT);
        viewportHeight.store(vpHeight);

        // AC-51/52: Element slicing with clamped scrollOffset
        int totalLines = static_cast<int>(chatElements.size());
        int maxScroll = std::max(0, totalLines - vpHeight);

        // Resolve scrollToBottom
        if (scrollToBottom.load()) {
            scrollOffset.store(maxScroll);
        }

        // AC-52: Clamp scrollOffset to [0, maxScroll] every frame
        int currentOffset = std::clamp(scrollOffset.load(), 0, maxScroll);
        scrollOffset.store(currentOffset);

        // AC-51: Slice visible elements only
        int endIdx = std::min(currentOffset + vpHeight, totalLines);
        std::vector<Element> visibleElements(chatElements.begin() + currentOffset,
                                              chatElements.begin() + endIdx);

        // AC-50: Manual element slicing replaces old proportional scroll approach
        auto chatBox = vbox(visibleElements) | flex;

        // Input area
        auto inputArea = hbox({
            text("> ") | bold | color(Color::Green),
            inputComponent->Render() | flex,
        }) | border;

        // Status bar
        auto statusBar = renderStatusBar(
            config_.getModel(),
            totalInputTokens_,
            totalOutputTokens_,
            calculateCost()
        );

        return vbox({
            chatBox | flex,
            separator(),
            inputArea,
            statusBar,
        });
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

        // Enter key - submit input
        if (event == Event::Return) {
            if (!inputText.empty() && !isGenerating_) {
                std::string msg = inputText;
                inputText.clear();

                {
                    std::lock_guard<std::mutex> lock(chatMutex);

                    // Check for /skill command
                    if (msg.substr(0, 6) == "/skill") {
                        handleSlashCommand(msg, chatMutex);
                        scrollToBottom.store(true);
                        return true;
                    }

                    handleInput(msg);
                    scrollToBottom.store(true); // AC-12: Auto-scroll on user message
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
                    }
                    screen.Post(Event::Custom);
                }).detach();
            }
            return true;
        }

        // Page Up - scroll up by one page (AC-55)
        if (event == Event::PageUp) {
            scrollOffset.store(scrollOffset.load() - viewportHeight.load());
            scrollToBottom.store(false);
            return true;
        }

        // Page Down - scroll down by one page (AC-55)
        if (event == Event::PageDown) {
            scrollOffset.store(scrollOffset.load() + viewportHeight.load());
            return true;
        }

        // Home - jump to top (AC-6)
        if (event == Event::Home) {
            scrollOffset.store(0);
            scrollToBottom.store(false);
            return true;
        }

        // End - jump to bottom (AC-7)
        if (event == Event::End) {
            scrollToBottom.store(true);
            return true;
        }

        // Mouse wheel scroll (AC-8, AC-9)
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                scrollOffset.store(scrollOffset.load() - MOUSE_SCROLL_LINES);
                scrollToBottom.store(false);
                return true;
            }
            if (event.mouse().button == Mouse::WheelDown) {
                scrollOffset.store(scrollOffset.load() + MOUSE_SCROLL_LINES);
                return true;
            }
        }

        // Up arrow - scroll when input empty, otherwise previous input
        if (event == Event::ArrowUp) {
            if (inputText.empty() || isGenerating_) {
                scrollOffset.store(scrollOffset.load() - 1);
                scrollToBottom.store(false);
                return true;
            }
            std::string prev = inputHandler_.getPreviousInput();
            if (!prev.empty()) inputText = prev;
            return true;
        }

        // Down arrow - scroll when input empty, otherwise next input
        if (event == Event::ArrowDown) {
            if (inputText.empty() || isGenerating_) {
                scrollOffset.store(scrollOffset.load() + 1);
                return true;
            }
            inputText = inputHandler_.getNextInput();
            return true;
        }

        return false;
    });

    screen.Loop(component);
}

} // namespace opencodecpp
