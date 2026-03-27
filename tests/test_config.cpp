#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cassert>
#include <sys/stat.h>
#include "config/config.h"

using namespace opencodecpp;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    tests_run++; \
    try { test_##name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED (unknown exception)\n"; }

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x)
#define ASSERT_FALSE(x) if (x) throw std::runtime_error(std::string("Assertion failed: !") + #x)
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + " (got '" + std::string(a) + "' vs '" + std::string(b) + "')")

// AC-5: Config loads from ~/.opencode/config.json
void test_config_dir_path() {
    std::string dir = Config::configDir();
    ASSERT_FALSE(dir.empty());
    // Should end with .opencode
    ASSERT_TRUE(dir.find(".opencode") != std::string::npos);
}

void test_config_file_path() {
    std::string path = Config::configFilePath();
    ASSERT_TRUE(path.find("config.json") != std::string::npos);
}

void test_config_db_path() {
    std::string path = Config::dbFilePath();
    ASSERT_TRUE(path.find("sessions.db") != std::string::npos);
}

// AC-5: Config loads defaults
void test_config_defaults() {
    Config cfg;
    ASSERT_FALSE(cfg.default_model.empty());
    ASSERT_FALSE(cfg.default_provider.empty());
}

// AC-6: Reads ANTHROPIC_API_KEY from env
void test_env_anthropic_key() {
    setenv("ANTHROPIC_API_KEY", "test-key-anthropic-123", 1);
    Config cfg;
    cfg.applyEnvOverrides();
    ASSERT_EQ(cfg.anthropic_api_key, std::string("test-key-anthropic-123"));
    unsetenv("ANTHROPIC_API_KEY");
}

// AC-6: Reads OPENAI_API_KEY from env
void test_env_openai_key() {
    setenv("OPENAI_API_KEY", "test-key-openai-456", 1);
    Config cfg;
    cfg.applyEnvOverrides();
    ASSERT_EQ(cfg.openai_api_key, std::string("test-key-openai-456"));
    unsetenv("OPENAI_API_KEY");
}

// AC-7: CLI --model overrides config
void test_cli_model_override() {
    const char* argv[] = {"opencode", "--model", "gpt-4o"};
    Config cfg = Config::fromArgs(3, const_cast<char**>(argv));
    ASSERT_EQ(cfg.getModel(), std::string("gpt-4o"));
}

// AC-7: CLI --provider overrides config
void test_cli_provider_override() {
    const char* argv[] = {"opencode", "--provider", "openai"};
    Config cfg = Config::fromArgs(3, const_cast<char**>(argv));
    ASSERT_EQ(cfg.getProvider(), std::string("openai"));
}

// AC-7: getModel returns default when no CLI override
void test_get_model_default() {
    Config cfg;
    ASSERT_TRUE(cfg.cli_model.empty());
    ASSERT_EQ(cfg.getModel(), cfg.default_model);
}

// AC-7: getProvider returns default when no CLI override
void test_get_provider_default() {
    Config cfg;
    ASSERT_TRUE(cfg.cli_provider.empty());
    ASSERT_EQ(cfg.getProvider(), cfg.default_provider);
}

// Config load creates directory
void test_config_load_creates_dir() {
    Config cfg;
    cfg.load();
    struct stat st;
    ASSERT_TRUE(stat(Config::configDir().c_str(), &st) == 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
}

int main() {
    std::cout << "=== Config Tests ===\n";
    TEST(config_dir_path);
    TEST(config_file_path);
    TEST(config_db_path);
    TEST(config_defaults);
    TEST(env_anthropic_key);
    TEST(env_openai_key);
    TEST(cli_model_override);
    TEST(cli_provider_override);
    TEST(get_model_default);
    TEST(get_provider_default);
    TEST(config_load_creates_dir);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
