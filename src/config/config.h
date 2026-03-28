#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace opencodecpp {

struct Config {
    std::string default_model = "claude-sonnet-4-20250514";
    std::string default_provider = "anthropic";
    std::string anthropic_api_key;
    std::string openai_api_key;
    std::string openai_base_url = "https://api.openai.com";
    std::string theme = "dark";
    int max_tokens = 4096;
    int context_window = 128000;
    int compact_threshold = 102400;
    std::string custom_instructions;
    std::string session_id;

    // CLI overrides
    std::string cli_model;
    std::string cli_provider;

    std::string getModel() const { return cli_model.empty() ? default_model : cli_model; }
    std::string getProvider() const { return cli_provider.empty() ? default_provider : cli_provider; }

    static std::string configDir();
    static std::string configFilePath();
    static std::string dbFilePath();

    void load();
    void save() const;
    void applyEnvOverrides();

    static Config fromArgs(int argc, char* argv[]);
};

} // namespace opencodecpp
