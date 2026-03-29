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
#include "../tools/web_search.h"
#include "../tools/web_fetch.h"
#include "../tools/subagent.h"
#include "../auth/codex_auth.h"

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
        } else {
            // Session loaded successfully, populate chat display
            loadSessionToChat();
        }
    } else {
        session_.createSession();
    }

    // Create LLM provider
    std::string provider = config_.getProvider();

    // When auth_mode is "codex", use Codex auth (Authorization Code + PKCE flow)
    bool codexMode = (config_.auth_mode == "codex");
    std::string codexAccountId;
    if (codexMode) {
        // Try cached token, check expiration, auto-refresh if needed
        auto tokenData = CodexAuth::loadCachedTokenData();
        std::string token;
        if (tokenData.contains("access_token")) {
            long long expiresAt = tokenData.value("expires_at", (long long)0);
            if (!CodexAuth::shouldRefresh(expiresAt)) {
                token = tokenData["access_token"].get<std::string>();
                codexAccountId = tokenData.value("account_id", "");
            } else if (tokenData.contains("refresh_token")) {
                // AC-12: Auto-refresh expired token
                token = CodexAuth::refreshToken(tokenData["refresh_token"].get<std::string>());
                if (!token.empty()) {
                    auto refreshedData = CodexAuth::loadCachedTokenData();
                    codexAccountId = refreshedData.value("account_id", "");
                } else {
                    // AC-13: Refresh failure clears cache and re-authenticates
                    remove(CodexAuth::tokenCachePath().c_str());
                }
            }
        }
        if (token.empty()) {
            token = CodexAuth::authenticate();
            if (!token.empty()) {
                codexAccountId = CodexAuth::extractAccountId(token);
                if (codexAccountId.empty()) {
                    auto cachedData = CodexAuth::loadCachedTokenData();
                    codexAccountId = cachedData.value("account_id", "");
                }
            }
        }
        if (!token.empty()) {
            config_.openai_api_key = token;
            config_.openai_base_url = CodexAuth::CODEX_BASE_URL;
            provider = "openai";
        }
    }

    if (provider == "openai") {
        auto openaiProvider = std::make_shared<OpenAIProvider>(
            config_.openai_api_key, config_.getModel(), config_.openai_base_url
        );
        // AC-8/AC-9: Enable codex mode with account ID for proper headers and endpoint
        if (codexMode && !codexAccountId.empty()) {
            openaiProvider->setCodexMode(true, codexAccountId);
        }
        provider_ = openaiProvider;
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
    agentLoop_->registerTool(std::make_shared<WebSearchTool>());
    agentLoop_->registerTool(std::make_shared<WebFetchTool>());

    // AC-15: Register subagent tool with provider and available tools
    auto subagentTool = std::make_shared<SubagentTool>();
    subagentTool->setProvider(provider_);
    agentLoop_->registerTool(subagentTool);
    // After registering all tools, set available tools on subagent
    subagentTool->setAvailableTools(agentLoop_->getTools());
}

void TUI::loadSessionToChat() {
    auto history = session_.getHistory();
    for (const auto& msg : history) {
        if (msg.role == "user" || msg.role == "assistant") {
            ChatMessage chatMsg;
            chatMsg.role = msg.role;
            // Extract text content from JSON
            if (msg.content.is_string()) {
                chatMsg.content = msg.content.get<std::string>();
            } else if (msg.content.is_array()) {
                // Concatenate text parts
                for (const auto& part : msg.content) {
                    if (part.contains("text")) chatMsg.content += part["text"].get<std::string>();
                }
            }
            chatMessages_.push_back(chatMsg);
            // Accumulate token counts
            totalInputTokens_ += msg.input_tokens;
            totalOutputTokens_ += msg.output_tokens;
        }
    }
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

    // Handle /plan command
    if (input.substr(0, 5) == "/plan") {
        std::string prompt = input.size() > 6 ? input.substr(6) : "";
        size_t start = prompt.find_first_not_of(" \t");
        if (start != std::string::npos) prompt = prompt.substr(start);
        else prompt.clear();

        if (prompt.empty()) {
            ChatMessage sysMsg;
            sysMsg.role = "tool";
            sysMsg.toolName = "plan";
            sysMsg.toolStatus = "done";
            sysMsg.content = "Usage: /plan <prompt>";
            chatMessages_.push_back(sysMsg);
            return true;
        }

        planMode_.store(true);
        lastPlanPrompt_ = prompt;

        ChatMessage startMsg;
        startMsg.role = "tool";
        startMsg.toolName = "plan";
        startMsg.toolStatus = "running";
        startMsg.content = "Planning: " + prompt;
        chatMessages_.push_back(startMsg);
        return true; // Will be handled asynchronously
    }

    // Handle /execute command
    if (input.substr(0, 8) == "/execute") {
        if (lastPlanOutput_.empty()) {
            ChatMessage sysMsg;
            sysMsg.role = "tool";
            sysMsg.toolName = "execute";
            sysMsg.toolStatus = "done";
            sysMsg.content = "Error: No plan available. Run /plan <prompt> first.";
            chatMessages_.push_back(sysMsg);
            return true;
        }

        ChatMessage startMsg;
        startMsg.role = "tool";
        startMsg.toolName = "execute";
        startMsg.toolStatus = "running";
        startMsg.content = "Executing plan...";
        chatMessages_.push_back(startMsg);
        return true; // Will be handled asynchronously
    }

    // Handle /sessions command (AC-14)
    if (input.substr(0, 9) == "/sessions") {
        auto sessions = session_.listSessions(20);
        ChatMessage sysMsg;
        sysMsg.role = "tool";
        sysMsg.toolName = "sessions";
        sysMsg.toolStatus = "done";

        if (sessions.empty()) {
            sysMsg.content = "No sessions found.";
        } else {
            std::string content = "Sessions:\n";
            int idx = 1;
            for (auto& s : sessions) {
                std::string truncId = s.session_id.substr(0, std::min((size_t)8, s.session_id.size()));
                std::string preview = s.last_message_preview;
                if (preview.size() > 80) preview = preview.substr(0, 80);
                content += "  [" + std::to_string(idx) + "] " + truncId
                    + "  " + s.created_at
                    + "  (" + std::to_string(s.message_count) + " msgs)";
                if (!preview.empty()) {
                    content += "  Last: " + preview;
                }
                content += "\n";
                idx++;
            }
            content += "\nUse /resume <session-id> to switch";
            sysMsg.content = content;
        }
        chatMessages_.push_back(sysMsg);
        return true;
    }

    // Handle /resume command (AC-15, AC-16)
    if (input.substr(0, 7) == "/resume") {
        std::string arg = input.size() > 8 ? input.substr(8) : "";
        // Trim
        size_t argStart = arg.find_first_not_of(" \t");
        if (argStart != std::string::npos) arg = arg.substr(argStart);
        else arg.clear();

        // AC-15: /resume without arg shows session list (same as /sessions)
        if (arg.empty()) {
            auto sessions = session_.listSessions(20);
            ChatMessage sysMsg;
            sysMsg.role = "tool";
            sysMsg.toolName = "resume";
            sysMsg.toolStatus = "done";

            if (sessions.empty()) {
                sysMsg.content = "No sessions found.";
            } else {
                std::string content = "Sessions:\n";
                int idx = 1;
                for (auto& s : sessions) {
                    std::string truncId = s.session_id.substr(0, std::min((size_t)8, s.session_id.size()));
                    std::string preview = s.last_message_preview;
                    if (preview.size() > 80) preview = preview.substr(0, 80);
                    content += "  [" + std::to_string(idx) + "] " + truncId
                        + "  " + s.created_at
                        + "  (" + std::to_string(s.message_count) + " msgs)";
                    if (!preview.empty()) {
                        content += "  Last: " + preview;
                    }
                    content += "\n";
                    idx++;
                }
                content += "\nUse /resume <session-id> to switch";
                sysMsg.content = content;
            }
            chatMessages_.push_back(sysMsg);
            return true;
        }

        // AC-16: /resume <session-id> loads the specified session
        if (session_.loadSession(arg)) {
            chatMessages_.clear();
            // Reset token counts before restoring
            totalInputTokens_ = 0;
            totalOutputTokens_ = 0;
            loadSessionToChat();

            int msgCount = session_.messageCount();
            ChatMessage sysMsg;
            sysMsg.role = "tool";
            sysMsg.toolName = "resume";
            sysMsg.toolStatus = "done";
            sysMsg.content = "Resumed session " + arg + " (" + std::to_string(msgCount) + " messages loaded)";
            chatMessages_.push_back(sysMsg);
        } else {
            ChatMessage sysMsg;
            sysMsg.role = "tool";
            sysMsg.toolName = "resume";
            sysMsg.toolStatus = "done";
            sysMsg.content = "Error: Session not found: " + arg;
            chatMessages_.push_back(sysMsg);
        }
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
    int selected_ = 0;  // ScrollableContainer selected index

    // Input component
    auto inputOption = InputOption();
    inputOption.multiline = false;
    auto inputComponent = Input(&inputText, "Type your message...", inputOption);

    // ScrollableContainer: a true FTXUI Component that wraps chat messages
    // This replaces the DOM-only yframe approach so scroll events are properly handled
    auto scrollableContainer = Make<ComponentBase>();

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

        // ScrollableContainer: auto-scroll when scrollToBottom is true
        if (scrollToBottom.load() && !chatElements.empty()) {
            selected_ = static_cast<int>(chatElements.size()) - 1;
        }

        // Clamp selected_
        if (selected_ < 0) selected_ = 0;
        if (!chatElements.empty() && selected_ >= static_cast<int>(chatElements.size())) {
            selected_ = static_cast<int>(chatElements.size()) - 1;
        }

        // Apply focus to selected element for yframe scrolling
        if (!chatElements.empty() && selected_ >= 0 && selected_ < static_cast<int>(chatElements.size())) {
            chatElements[selected_] = chatElements[selected_] | focus;
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

        // Status bar with plan mode indicator
        auto statusBar = renderStatusBar(
            config_.getModel(),
            totalInputTokens_,
            totalOutputTokens_,
            calculateCost(),
            planMode_.load()
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

    // Event handler - ScrollableContainer handles scroll events as a Component
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

            // AC-11, AC-12, AC-13: Steer input during generation
            if (!inputText.empty() && isGenerating_) {
                std::string steerText = inputText;
                inputText.clear();
                showSuggestions_ = false;
                suggestionIndex_ = 0;

                {
                    std::lock_guard<std::mutex> lock(chatMutex);
                    // AC-13: Display steer message with [Steer] prefix
                    ChatMessage steerMsg;
                    steerMsg.role = "user";
                    steerMsg.content = "[Steer] " + steerText;
                    chatMessages_.push_back(steerMsg);

                    // AC-12: Queue steer for injection into agent loop
                    agentLoop_->addSteer(steerText);
                    scrollToBottom.store(true);
                }
                return true;
            }

            if (!inputText.empty() && !isGenerating_) {
                std::string msg = inputText;
                inputText.clear();
                showSuggestions_ = false;
                suggestionIndex_ = 0;

                {
                    std::lock_guard<std::mutex> lock(chatMutex);

                    // Check for slash commands
                    if (msg[0] == '/' && handleSlashCommand(msg, chatMutex)) {
                        // /compact needs async handling
                        if (msg.substr(0, 8) == "/compact") {
                            // Will run compact in background thread below
                        }
                        // /plan needs async handling
                        else if (msg.substr(0, 5) == "/plan" && msg.size() > 6) {
                            // Will run plan in background thread below
                        }
                        // /execute needs async handling
                        else if (msg.substr(0, 8) == "/execute" && !lastPlanOutput_.empty()) {
                            // Will run execute in background thread below
                        }
                        else {
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

                    // If /plan, run in background with read-only tools
                    if (msg.substr(0, 5) == "/plan" && msg.size() > 6) {
                        scrollToBottom.store(true);
                        isGenerating_ = true;
                        cancelGeneration_ = false;

                        // Add assistant message placeholder
                        ChatMessage aiMsg;
                        aiMsg.role = "assistant";
                        aiMsg.content = "";
                        chatMessages_.push_back(aiMsg);

                        std::string planPrompt = msg.substr(6);
                        std::thread([this, planPrompt, &chatMutex, &screen, &scrollToBottom]() {
                            std::string systemPrompt =
                                "You are an AI coding assistant in PLAN MODE. "
                                "Analyze the codebase and create a structured implementation plan. "
                                "Do not modify any files. Only analyze, read, and plan. "
                                "Your output should be a clear, actionable plan that can be executed later.";

                            if (!config_.custom_instructions.empty()) {
                                systemPrompt += "\n\nCustom instructions: " + config_.custom_instructions;
                            }

                            AgentCallbacks callbacks;
                            callbacks.onToken = [this, &chatMutex, &screen](const std::string& token) {
                                std::lock_guard<std::mutex> lock(chatMutex);
                                if (!chatMessages_.empty() && chatMessages_.back().role == "assistant") {
                                    chatMessages_.back().content += token;
                                }
                                screen.Post(Event::Custom);
                            };
                            callbacks.onToolStatus = [this, &chatMutex, &screen](const std::string& name, const std::string& status) {
                                std::lock_guard<std::mutex> lock(chatMutex);
                                ChatMessage toolMsg;
                                toolMsg.role = "tool";
                                toolMsg.toolName = name;
                                toolMsg.toolStatus = status;
                                if (chatMessages_.size() >= 2) {
                                    chatMessages_.insert(chatMessages_.end() - 1, toolMsg);
                                }
                                screen.Post(Event::Custom);
                            };
                            callbacks.cancelCheck = [this]() -> bool {
                                return cancelGeneration_.load();
                            };

                            std::vector<std::string> allowedTools = {"file_read", "glob", "grep", "ls"};
                            auto resp = agentLoop_->runPlan(planPrompt, systemPrompt, allowedTools, callbacks);

                            {
                                std::lock_guard<std::mutex> lock(chatMutex);
                                totalInputTokens_ = agentLoop_->totalInputTokens();
                                totalOutputTokens_ = agentLoop_->totalOutputTokens();
                                isGenerating_ = false;
                                planMode_.store(false);

                                // Store plan output for /execute
                                if (!resp.content.empty()) {
                                    lastPlanOutput_ = resp.content;
                                } else if (!chatMessages_.empty() && chatMessages_.back().role == "assistant") {
                                    lastPlanOutput_ = chatMessages_.back().content;
                                }

                                if (!resp.error.empty()) {
                                    ChatMessage errMsg;
                                    errMsg.role = "assistant";
                                    errMsg.content = "Error: " + resp.error;
                                    chatMessages_.push_back(errMsg);
                                }

                                ChatMessage doneMsg;
                                doneMsg.role = "tool";
                                doneMsg.toolName = "plan";
                                doneMsg.toolStatus = "done";
                                doneMsg.content = "Plan complete. Use /execute to run the plan.";
                                chatMessages_.push_back(doneMsg);
                            }
                            screen.Post(Event::Custom);
                        }).detach();
                        return true;
                    }

                    // If /execute, run with full tool access
                    if (msg.substr(0, 8) == "/execute" && !lastPlanOutput_.empty()) {
                        scrollToBottom.store(true);
                        isGenerating_ = true;
                        cancelGeneration_ = false;

                        std::string executePrompt = "Execute the following plan:\n\n" + lastPlanOutput_ +
                            "\n\nOriginal request: " + lastPlanPrompt_;

                        // Add user message and assistant placeholder
                        ChatMessage userMsg;
                        userMsg.role = "user";
                        userMsg.content = executePrompt;
                        chatMessages_.push_back(userMsg);

                        ChatMessage aiMsg;
                        aiMsg.role = "assistant";
                        aiMsg.content = "";
                        chatMessages_.push_back(aiMsg);

                        std::thread([this, executePrompt, &chatMutex, &screen, &scrollToBottom]() {
                            std::string systemPrompt =
                                "You are an AI coding assistant. Help the user with their programming tasks. "
                                "You have access to tools for reading, writing, and editing files, "
                                "running shell commands, and searching the codebase.";

                            if (!config_.custom_instructions.empty()) {
                                systemPrompt += "\n\nCustom instructions: " + config_.custom_instructions;
                            }

                            std::string skillPrompt = skillManager_.getActiveSkillPrompt();
                            if (!skillPrompt.empty()) {
                                systemPrompt += skillPrompt;
                            }

                            AgentCallbacks callbacks;
                            callbacks.onToken = [this, &chatMutex, &screen](const std::string& token) {
                                std::lock_guard<std::mutex> lock(chatMutex);
                                if (!chatMessages_.empty() && chatMessages_.back().role == "assistant") {
                                    chatMessages_.back().content += token;
                                }
                                screen.Post(Event::Custom);
                            };
                            callbacks.onToolStatus = [this, &chatMutex, &screen](const std::string& name, const std::string& status) {
                                std::lock_guard<std::mutex> lock(chatMutex);
                                ChatMessage toolMsg;
                                toolMsg.role = "tool";
                                toolMsg.toolName = name;
                                toolMsg.toolStatus = status;
                                if (chatMessages_.size() >= 2) {
                                    chatMessages_.insert(chatMessages_.end() - 1, toolMsg);
                                }
                                screen.Post(Event::Custom);
                            };
                            callbacks.cancelCheck = [this]() -> bool {
                                return cancelGeneration_.load();
                            };

                            auto resp = agentLoop_->run(executePrompt, systemPrompt, callbacks);

                            {
                                std::lock_guard<std::mutex> lock(chatMutex);
                                totalInputTokens_ = agentLoop_->totalInputTokens();
                                totalOutputTokens_ = agentLoop_->totalOutputTokens();
                                isGenerating_ = false;
                                planMode_.store(false);

                                if (!resp.error.empty()) {
                                    ChatMessage errMsg;
                                    errMsg.role = "assistant";
                                    errMsg.content = "Error: " + resp.error;
                                    chatMessages_.push_back(errMsg);
                                }
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

        // ScrollableContainer event handling - these return true to consume the event
        // This is the key fix: events are handled by our Component, not passed to inputComponent

        // Page Up - scroll up
        if (event == Event::PageUp) {
            std::lock_guard<std::mutex> lock(chatMutex);
            scrollToBottom.store(false);
            selected_ = std::max(0, selected_ - 10);
            return true;
        }

        // Page Down - scroll down
        if (event == Event::PageDown) {
            std::lock_guard<std::mutex> lock(chatMutex);
            int maxIdx = static_cast<int>(chatMessages_.size()) * 2 - 1;
            selected_ = std::min(maxIdx, selected_ + 10);
            return true;
        }

        // Home - jump to top
        if (event == Event::Home) {
            std::lock_guard<std::mutex> lock(chatMutex);
            scrollToBottom.store(false);
            selected_ = 0;
            return true;
        }

        // End - jump to bottom, re-enable auto-scroll
        if (event == Event::End) {
            std::lock_guard<std::mutex> lock(chatMutex);
            scrollToBottom.store(true);
            int maxIdx = static_cast<int>(chatMessages_.size()) * 2 - 1;
            selected_ = std::max(0, maxIdx);
            return true;
        }

        // Mouse wheel scroll - handled by ScrollableContainer Component
        if (event.is_mouse()) {
            if (event.mouse().button == Mouse::WheelUp) {
                std::lock_guard<std::mutex> lock(chatMutex);
                scrollToBottom.store(false);
                selected_ = std::max(0, selected_ - 3);
                return true;
            }
            if (event.mouse().button == Mouse::WheelDown) {
                std::lock_guard<std::mutex> lock(chatMutex);
                int maxIdx = static_cast<int>(chatMessages_.size()) * 2 - 1;
                selected_ = std::min(std::max(0, maxIdx), selected_ + 3);
                return true;
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
                std::lock_guard<std::mutex> lock(chatMutex);
                scrollToBottom.store(false);
                selected_ = std::max(0, selected_ - 1);
                return true;
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
                std::lock_guard<std::mutex> lock(chatMutex);
                int maxIdx = static_cast<int>(chatMessages_.size()) * 2 - 1;
                selected_ = std::min(std::max(0, maxIdx), selected_ + 1);
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
