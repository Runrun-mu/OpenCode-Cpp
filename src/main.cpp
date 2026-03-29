#include "config/config.h"
#include "tui/tui.h"
#include "session/database.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // Parse args and load config (may exit for --version/--help)
    opencodecpp::Config config = opencodecpp::Config::fromArgs(argc, argv);

    // Initialize database (ensure tables are created)
    opencodecpp::Database db;
    db.open(opencodecpp::Config::dbFilePath());

    // AC-9: Resolve --resume flag to session_id
    if (config.resume) {
        if (!config.resume_session_id.empty()) {
            config.session_id = config.resume_session_id;
        } else {
            // --resume without arg: resolve to most recent session
            std::string recentId = db.getMostRecentSessionId();
            if (!recentId.empty()) {
                config.session_id = recentId;
            } else {
                std::cerr << "No previous sessions found to resume.\n";
            }
        }
    }

    db.close();

    // AC-8: Check API key availability
    std::string provider = config.getProvider();
    if (provider == "anthropic" && config.anthropic_api_key.empty()) {
        std::cerr << "Error: No API key found for Anthropic provider.\n"
                  << "Set the ANTHROPIC_API_KEY environment variable or add it to "
                  << opencodecpp::Config::configFilePath() << "\n";
        return 1;
    }
    if (provider == "openai" && config.openai_api_key.empty() && config.auth_mode != "codex") {
        std::cerr << "Error: No API key found for OpenAI provider.\n"
                  << "Set the OPENAI_API_KEY environment variable or add it to "
                  << opencodecpp::Config::configFilePath() << "\n";
        return 1;
    }

    // Launch TUI
    opencodecpp::TUI tui(config);
    tui.run();

    return 0;
}
