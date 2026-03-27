#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cassert>
#include <sys/stat.h>
#include <unistd.h>
#include "tools/bash.h"
#include "tools/file_read.h"
#include "tools/file_write.h"
#include "tools/file_edit.h"
#include "tools/glob.h"
#include "tools/grep.h"
#include "tools/ls.h"

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
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b)

static std::string tmpDir;

static void setupTmpDir() {
    char tmpl[] = "/tmp/opencode_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (d) tmpDir = d;
    else tmpDir = "/tmp/opencode_test_fallback";
    mkdir(tmpDir.c_str(), 0755);
}

static void cleanupTmpDir() {
    std::string cmd = "rm -rf " + tmpDir;
    system(cmd.c_str());
}

// === AC-18: Bash tool ===
void test_bash_echo() {
    BashTool bash;
    ASSERT_EQ(bash.name(), std::string("bash"));
    auto result = bash.execute({{"command", "echo hello"}});
    ASSERT_TRUE(result.contains("stdout"));
    std::string out = result["stdout"].get<std::string>();
    ASSERT_TRUE(out.find("hello") != std::string::npos);
    ASSERT_EQ(result["exit_code"].get<int>(), 0);
}

void test_bash_exit_code() {
    BashTool bash;
    auto result = bash.execute({{"command", "exit 42"}});
    ASSERT_EQ(result["exit_code"].get<int>(), 42);
}

void test_bash_timeout() {
    BashTool bash;
    auto result = bash.execute({{"command", "sleep 100"}, {"timeout", 2}});
    ASSERT_TRUE(result.contains("error"));
    std::string err = result["error"].get<std::string>();
    ASSERT_TRUE(err.find("timed out") != std::string::npos);
}

void test_bash_empty_command() {
    BashTool bash;
    auto result = bash.execute({{"command", ""}});
    ASSERT_TRUE(result.contains("error"));
}

void test_bash_schema() {
    BashTool bash;
    auto s = bash.schema();
    ASSERT_TRUE(s.contains("type"));
    ASSERT_TRUE(s.contains("properties"));
    ASSERT_TRUE(s["properties"].contains("command"));
}

// === AC-19: Read tool ===
void test_read_file() {
    FileReadTool reader;
    ASSERT_EQ(reader.name(), std::string("read"));

    // Create test file
    std::string path = tmpDir + "/test_read.txt";
    {
        std::ofstream f(path);
        f << "line1\nline2\nline3\n";
    }
    auto result = reader.execute({{"file_path", path}});
    ASSERT_TRUE(result.contains("content"));
    ASSERT_TRUE(result["content"].get<std::string>().find("line1") != std::string::npos);
    ASSERT_EQ(result["lines_read"].get<int>(), 3);
}

void test_read_with_offset_limit() {
    FileReadTool reader;
    std::string path = tmpDir + "/test_read_ol.txt";
    {
        std::ofstream f(path);
        f << "a\nb\nc\nd\ne\n";
    }
    auto result = reader.execute({{"file_path", path}, {"offset", 2}, {"limit", 2}});
    ASSERT_EQ(result["lines_read"].get<int>(), 2);
    std::string content = result["content"].get<std::string>();
    ASSERT_TRUE(content.find("b") != std::string::npos);
    ASSERT_TRUE(content.find("c") != std::string::npos);
}

void test_read_nonexistent() {
    FileReadTool reader;
    auto result = reader.execute({{"file_path", "/nonexistent/file.txt"}});
    ASSERT_TRUE(result.contains("error"));
}

// === AC-20: Write tool ===
void test_write_file() {
    FileWriteTool writer;
    ASSERT_EQ(writer.name(), std::string("write"));

    std::string path = tmpDir + "/test_write.txt";
    auto result = writer.execute({{"file_path", path}, {"content", "hello world"}});
    ASSERT_TRUE(result["success"].get<bool>());
    ASSERT_EQ(result["bytes_written"].get<int>(), 11);

    // Verify contents
    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_EQ(content, std::string("hello world"));
}

void test_write_creates_directories() {
    FileWriteTool writer;
    std::string path = tmpDir + "/subdir1/subdir2/test_write_dirs.txt";
    auto result = writer.execute({{"file_path", path}, {"content", "nested"}});
    ASSERT_TRUE(result["success"].get<bool>());

    std::ifstream f(path);
    ASSERT_TRUE(f.good());
}

// === AC-21: Edit tool ===
void test_edit_file() {
    FileEditTool editor;
    ASSERT_EQ(editor.name(), std::string("edit"));

    std::string path = tmpDir + "/test_edit.txt";
    {
        std::ofstream f(path);
        f << "foo bar baz";
    }

    auto result = editor.execute({{"file_path", path}, {"old_string", "bar"}, {"new_string", "qux"}});
    ASSERT_TRUE(result["success"].get<bool>());

    // Verify
    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(content.find("qux") != std::string::npos);
    ASSERT_TRUE(content.find("bar") == std::string::npos);
}

void test_edit_not_found() {
    FileEditTool editor;
    std::string path = tmpDir + "/test_edit_nf.txt";
    {
        std::ofstream f(path);
        f << "hello world";
    }
    auto result = editor.execute({{"file_path", path}, {"old_string", "xyz"}, {"new_string", "abc"}});
    ASSERT_TRUE(result.contains("error"));
}

// === AC-22: Glob tool ===
void test_glob_find_files() {
    GlobTool glob;
    ASSERT_EQ(glob.name(), std::string("glob"));

    // Create files
    {
        std::ofstream(tmpDir + "/a.cpp");
        std::ofstream(tmpDir + "/b.cpp");
        std::ofstream(tmpDir + "/c.txt");
    }

    auto result = glob.execute({{"pattern", "*.cpp"}, {"path", tmpDir}});
    ASSERT_TRUE(result.contains("files"));
    ASSERT_TRUE(result["count"].get<int>() >= 2);
}

// === AC-23: Grep tool ===
void test_grep_search() {
    GrepTool grep;
    ASSERT_EQ(grep.name(), std::string("grep"));

    std::string path = tmpDir + "/grep_test.txt";
    {
        std::ofstream f(path);
        f << "hello world\nfoo bar\nhello again\n";
    }

    auto result = grep.execute({{"pattern", "hello"}, {"path", path}});
    ASSERT_TRUE(result.contains("matches"));
    ASSERT_TRUE(result["count"].get<int>() >= 2);
}

// === AC-24: Ls tool ===
void test_ls_directory() {
    LsTool ls;
    ASSERT_EQ(ls.name(), std::string("ls"));

    auto result = ls.execute({{"path", tmpDir}});
    ASSERT_TRUE(result.contains("entries"));
    ASSERT_TRUE(result["entries"].is_array());
    ASSERT_TRUE(result["entries"].size() > 0);
}

void test_ls_nonexistent() {
    LsTool ls;
    auto result = ls.execute({{"path", "/nonexistent/dir"}});
    ASSERT_TRUE(result.contains("error"));
}

// Tool schemas
void test_tool_schemas() {
    BashTool bash;
    FileReadTool reader;
    FileWriteTool writer;
    FileEditTool editor;
    GlobTool glob;
    GrepTool grep;
    LsTool ls;

    auto check = [](const nlohmann::json& s, const std::string& name) {
        if (!s.contains("type") || s["type"] != "object")
            throw std::runtime_error(name + " schema missing type");
        if (!s.contains("properties"))
            throw std::runtime_error(name + " schema missing properties");
    };

    check(bash.schema(), "bash");
    check(reader.schema(), "read");
    check(writer.schema(), "write");
    check(editor.schema(), "edit");
    check(glob.schema(), "glob");
    check(grep.schema(), "grep");
    check(ls.schema(), "ls");
}

int main() {
    setupTmpDir();

    std::cout << "=== Tool Tests ===\n";

    // AC-18: Bash
    TEST(bash_echo);
    TEST(bash_exit_code);
    TEST(bash_timeout);
    TEST(bash_empty_command);
    TEST(bash_schema);

    // AC-19: Read
    TEST(read_file);
    TEST(read_with_offset_limit);
    TEST(read_nonexistent);

    // AC-20: Write
    TEST(write_file);
    TEST(write_creates_directories);

    // AC-21: Edit
    TEST(edit_file);
    TEST(edit_not_found);

    // AC-22: Glob
    TEST(glob_find_files);

    // AC-23: Grep
    TEST(grep_search);

    // AC-24: Ls
    TEST(ls_directory);
    TEST(ls_nonexistent);

    // Schemas
    TEST(tool_schemas);

    cleanupTmpDir();

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
