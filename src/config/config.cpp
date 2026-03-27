#include "config.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <sqlite3.h>

namespace opencodecpp {

std::string Config::configDir() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.opencode";
}

std::string Config::configFilePath() {
    return configDir() + "/config.json";
}

std::string Config::dbFilePath() {
    return configDir() + "/sessions.db";
}

void Config::load() {
    // Ensure directory exists
    std::string dir = configDir();
    mkdir(dir.c_str(), 0755);

    std::string path = configFilePath();
    std::ifstream f(path);
    if (f.good()) {
        try {
            nlohmann::json j;
            f >> j;
            if (j.contains("default_model")) default_model = j["default_model"].get<std::string>();
            if (j.contains("default_provider")) default_provider = j["default_provider"].get<std::string>();
            if (j.contains("theme")) theme = j["theme"].get<std::string>();
            if (j.contains("max_tokens")) max_tokens = j["max_tokens"].get<int>();
            if (j.contains("custom_instructions")) custom_instructions = j["custom_instructions"].get<std::string>();
            if (j.contains("api_keys")) {
                auto& keys = j["api_keys"];
                if (keys.contains("anthropic")) anthropic_api_key = keys["anthropic"].get<std::string>();
                if (keys.contains("openai")) openai_api_key = keys["openai"].get<std::string>();
            }
            if (j.contains("openai_base_url")) openai_base_url = j["openai_base_url"].get<std::string>();
        } catch (...) {
            // Use defaults on parse error
        }
    } else {
        // Create default config file
        save();
    }

    applyEnvOverrides();

    // Also initialize the database (create tables) during load
    // so that even --help/--version triggers DB creation
    {
        std::string dbPath = dbFilePath();
        // We just open and close to trigger table creation
        // This is done via a simple sqlite3 call
        sqlite3* db = nullptr;
        if (sqlite3_open(dbPath.c_str(), &db) == SQLITE_OK) {
            const char* sql1 =
                "CREATE TABLE IF NOT EXISTS sessions ("
                "  session_id TEXT PRIMARY KEY,"
                "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                ");";
            const char* sql2 =
                "CREATE TABLE IF NOT EXISTS messages ("
                "  id TEXT PRIMARY KEY,"
                "  session_id TEXT NOT NULL,"
                "  role TEXT NOT NULL,"
                "  content TEXT,"
                "  tool_calls TEXT,"
                "  tool_results TEXT,"
                "  timestamp TEXT NOT NULL DEFAULT (datetime('now')),"
                "  token_count INTEGER DEFAULT 0,"
                "  FOREIGN KEY (session_id) REFERENCES sessions(session_id)"
                ");";
            sqlite3_exec(db, sql1, nullptr, nullptr, nullptr);
            sqlite3_exec(db, sql2, nullptr, nullptr, nullptr);
            sqlite3_close(db);
        }
    }
}

void Config::save() const {
    std::string dir = configDir();
    mkdir(dir.c_str(), 0755);

    nlohmann::json j;
    j["default_model"] = default_model;
    j["default_provider"] = default_provider;
    j["theme"] = theme;
    j["max_tokens"] = max_tokens;
    j["custom_instructions"] = custom_instructions;
    j["api_keys"] = {
        {"anthropic", anthropic_api_key},
        {"openai", openai_api_key}
    };
    j["openai_base_url"] = openai_base_url;

    std::ofstream f(configFilePath());
    if (f.good()) {
        f << j.dump(2) << std::endl;
    }
}

void Config::applyEnvOverrides() {
    const char* akey = std::getenv("ANTHROPIC_API_KEY");
    if (akey && akey[0]) anthropic_api_key = akey;

    const char* okey = std::getenv("OPENAI_API_KEY");
    if (okey && okey[0]) openai_api_key = okey;
}

Config Config::fromArgs(int argc, char* argv[]) {
    Config cfg;

    bool showVersion = false;
    bool showHelp = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            showVersion = true;
        } else if (arg == "--help" || arg == "-h") {
            showHelp = true;
        } else if (arg == "--model" && i + 1 < argc) {
            cfg.cli_model = argv[++i];
        } else if (arg == "--provider" && i + 1 < argc) {
            cfg.cli_provider = argv[++i];
        } else if (arg == "--session" && i + 1 < argc) {
            cfg.session_id = argv[++i];
        }
    }

    // Load config from file and env
    cfg.load();

    if (showVersion) {
        std::cout << "opencode v" << OPENCODECPP_VERSION << std::endl;
        std::exit(0);
    }

    if (showHelp) {
        std::cout << "opencode v" << OPENCODECPP_VERSION << " - AI Coding Agent\n\n"
                  << "Usage: opencode [OPTIONS]\n\n"
                  << "Options:\n"
                  << "  --model <model>       Specify LLM model (e.g. claude-sonnet-4-20250514, gpt-4o)\n"
                  << "  --provider <provider> Specify provider (anthropic, openai)\n"
                  << "  --session <id>        Resume a previous session by ID\n"
                  << "  --version, -v         Show version number\n"
                  << "  --help, -h            Show this help message\n\n"
                  << "Environment variables:\n"
                  << "  ANTHROPIC_API_KEY     Anthropic API key\n"
                  << "  OPENAI_API_KEY        OpenAI API key\n\n"
                  << "Config file: " << configFilePath() << "\n";
        std::exit(0);
    }

    return cfg;
}

} // namespace opencodecpp
