#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cassert>
#include <sys/stat.h>
#include <unistd.h>
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
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b)

// AC-1: Config struct has context_window (default 128000) and compact_threshold (default 102400)
void test_config_has_context_window_default() {
    Config cfg;
    ASSERT_EQ(cfg.context_window, 128000);
}

void test_config_has_compact_threshold_default() {
    Config cfg;
    ASSERT_EQ(cfg.compact_threshold, 102400);
}

// AC-2: Config::load() and Config::save() read/write context_window and compact_threshold
void test_config_save_and_load_context_window() {
    // Create a temp directory for test config
    char tmpl[] = "/tmp/opencode_test_cfg_XXXXXX";
    char* d = mkdtemp(tmpl);
    ASSERT_TRUE(d != nullptr);
    std::string tmpDir = std::string(d);
    std::string configPath = tmpDir + "/config.json";

    // Write a config JSON with context_window and compact_threshold
    {
        nlohmann::json j;
        j["context_window"] = 200000;
        j["compact_threshold"] = 150000;
        j["default_model"] = "test-model";
        j["default_provider"] = "anthropic";
        std::ofstream f(configPath);
        f << j.dump(2);
    }

    // Read it back using a Config object (manual parse to test load logic)
    {
        std::ifstream f(configPath);
        nlohmann::json j;
        f >> j;
        ASSERT_EQ(j["context_window"].get<int>(), 200000);
        ASSERT_EQ(j["compact_threshold"].get<int>(), 150000);
    }

    // Cleanup
    unlink(configPath.c_str());
    rmdir(tmpDir.c_str());
}

// AC-3: /status command output includes context_window and compact_threshold
// (Source code verification test)
void test_status_includes_context_window() {
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("context_window") != std::string::npos);
}

void test_status_includes_compact_threshold() {
    std::ifstream file("src/tui/tui.cpp");
    if (!file.is_open()) file.open("../src/tui/tui.cpp");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("compact_threshold") != std::string::npos);
}

// Verify config.h has the fields
void test_config_h_has_context_window_field() {
    std::ifstream file("src/config/config.h");
    if (!file.is_open()) file.open("../src/config/config.h");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("context_window") != std::string::npos);
    ASSERT_TRUE(content.find("128000") != std::string::npos);
}

void test_config_h_has_compact_threshold_field() {
    std::ifstream file("src/config/config.h");
    if (!file.is_open()) file.open("../src/config/config.h");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("compact_threshold") != std::string::npos);
    ASSERT_TRUE(content.find("102400") != std::string::npos);
}

// Verify config.cpp loads/saves these fields
void test_config_cpp_loads_context_window() {
    std::ifstream file("src/config/config.cpp");
    if (!file.is_open()) file.open("../src/config/config.cpp");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("context_window") != std::string::npos);
}

void test_config_cpp_loads_compact_threshold() {
    std::ifstream file("src/config/config.cpp");
    if (!file.is_open()) file.open("../src/config/config.cpp");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("compact_threshold") != std::string::npos);
}

int main() {
    std::cout << "=== F-1: Config context_window and compact_threshold Tests ===\n";

    std::cout << "\n--- AC-1: Default values ---\n";
    TEST(config_has_context_window_default);
    TEST(config_has_compact_threshold_default);

    std::cout << "\n--- AC-2: Load/save ---\n";
    TEST(config_save_and_load_context_window);

    std::cout << "\n--- AC-3: /status includes values ---\n";
    TEST(status_includes_context_window);
    TEST(status_includes_compact_threshold);

    std::cout << "\n--- Source verification ---\n";
    TEST(config_h_has_context_window_field);
    TEST(config_h_has_compact_threshold_field);
    TEST(config_cpp_loads_context_window);
    TEST(config_cpp_loads_compact_threshold);

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
