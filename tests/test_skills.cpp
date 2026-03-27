#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include "skills/skill.h"
#include "skills/skill_tool.h"

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
    char tmpl[] = "/tmp/opencode_skill_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (d) tmpDir = d;
    else tmpDir = "/tmp/opencode_skill_test_fallback";
    mkdir(tmpDir.c_str(), 0755);
}

static void cleanupTmpDir() {
    std::string cmd = "rm -rf " + tmpDir;
    system(cmd.c_str());
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    f.close();
}

// === AC-112: YAML Frontmatter parsing ===
void test_frontmatter_parsing() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    std::string skillContent =
        "---\n"
        "name: test-skill\n"
        "description: A test skill\n"
        "globs:\n"
        "  - \"*.py\"\n"
        "  - \"**/*.py\"\n"
        "---\n"
        "\n"
        "# Test Skill\n"
        "\n"
        "This is the skill body.\n";

    std::string path = skillDir + "/test.md";
    writeFile(path, skillContent);

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    auto& skills = mgr.getAllSkills();

    ASSERT_TRUE(skills.size() >= 1);
    ASSERT_EQ(skills[0].name, std::string("test-skill"));
    ASSERT_EQ(skills[0].description, std::string("A test skill"));
    ASSERT_TRUE(skills[0].globs.size() >= 2);
    ASSERT_EQ(skills[0].globs[0], std::string("*.py"));
    ASSERT_EQ(skills[0].globs[1], std::string("**/*.py"));

    cleanupTmpDir();
}

// === AC-113: Skill body parsing ===
void test_skill_body() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    std::string skillContent =
        "---\n"
        "name: body-test\n"
        "description: Body test\n"
        "globs: []\n"
        "---\n"
        "\n"
        "# Body Content\n"
        "\n"
        "Line 1\n"
        "Line 2\n";

    writeFile(skillDir + "/body.md", skillContent);

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    auto& skills = mgr.getAllSkills();

    ASSERT_TRUE(skills.size() >= 1);
    // Body should contain the markdown after frontmatter
    ASSERT_TRUE(skills[0].content.find("# Body Content") != std::string::npos);
    ASSERT_TRUE(skills[0].content.find("Line 1") != std::string::npos);
}

// === AC-114/115: Skill discovery ===
void test_skill_discovery_from_directory() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/a.md",
        "---\nname: skill-a\ndescription: Skill A\nglobs: []\n---\n\nContent A\n");
    writeFile(skillDir + "/b.md",
        "---\nname: skill-b\ndescription: Skill B\nglobs: []\n---\n\nContent B\n");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    ASSERT_TRUE(mgr.getAllSkills().size() >= 2);

    cleanupTmpDir();
}

void test_invalid_skill_file_skipped() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/invalid.md", "This has no frontmatter at all");
    writeFile(skillDir + "/valid.md",
        "---\nname: valid\ndescription: Valid\nglobs: []\n---\n\nBody\n");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    // Should have at least the valid one, invalid skipped
    bool foundValid = false;
    for (auto& s : mgr.getAllSkills()) {
        if (s.name == "valid") foundValid = true;
    }
    ASSERT_TRUE(foundValid);

    cleanupTmpDir();
}

// === AC-116: Auto-activate with glob matching ===
void test_auto_activate_glob() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/py.md",
        "---\nname: python-skill\ndescription: Python\nglobs:\n  - \"*.py\"\n---\n\nPython skill\n");

    // Create a .py file in tmpDir
    writeFile(tmpDir + "/test.py", "print('hello')");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    mgr.autoActivate(tmpDir);

    auto* skill = mgr.findByName("python-skill");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_TRUE(skill->active);

    cleanupTmpDir();
}

void test_auto_activate_no_match() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/py.md",
        "---\nname: python-skill\ndescription: Python\nglobs:\n  - \"*.py\"\n---\n\nPython skill\n");

    // Create only .cpp files, no .py files
    writeFile(tmpDir + "/test.cpp", "int main() {}");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    mgr.autoActivate(tmpDir);

    auto* skill = mgr.findByName("python-skill");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_FALSE(skill->active);

    cleanupTmpDir();
}

// === AC-117: Activate by name ===
void test_activate_by_name() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/test.md",
        "---\nname: my-skill\ndescription: My\nglobs: []\n---\n\nBody\n");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    ASSERT_TRUE(mgr.activate("my-skill"));
    auto* skill = mgr.findByName("my-skill");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_TRUE(skill->active);
}

void test_activate_nonexistent() {
    SkillManager mgr;
    ASSERT_FALSE(mgr.activate("does-not-exist"));
}

// === AC-120: System prompt injection ===
void test_active_skill_prompt() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/a.md",
        "---\nname: skill-a\ndescription: A\nglobs: []\n---\n\nContent A\n");
    writeFile(skillDir + "/b.md",
        "---\nname: skill-b\ndescription: B\nglobs: []\n---\n\nContent B\n");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    mgr.activate("skill-a");
    mgr.activate("skill-b");

    std::string prompt = mgr.getActiveSkillPrompt();
    ASSERT_TRUE(prompt.find("--- Skill: skill-a ---") != std::string::npos);
    ASSERT_TRUE(prompt.find("Content A") != std::string::npos);
    ASSERT_TRUE(prompt.find("--- Skill: skill-b ---") != std::string::npos);
    ASSERT_TRUE(prompt.find("Content B") != std::string::npos);

    cleanupTmpDir();
}

void test_no_active_skills_empty_prompt() {
    SkillManager mgr;
    std::string prompt = mgr.getActiveSkillPrompt();
    ASSERT_TRUE(prompt.empty());
}

// === AC-122/123/124: SkillTool ===
void test_skill_tool_name() {
    SkillManager mgr;
    SkillTool tool(mgr);
    ASSERT_EQ(tool.name(), std::string("skill"));
}

void test_skill_tool_schema() {
    SkillManager mgr;
    SkillTool tool(mgr);
    auto schema = tool.schema();
    ASSERT_TRUE(schema.contains("properties"));
    ASSERT_TRUE(schema["properties"].contains("action"));
}

void test_skill_tool_list_action() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/test.md",
        "---\nname: listed-skill\ndescription: A listed skill\nglobs: []\n---\n\nBody\n");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    SkillTool tool(mgr);

    auto result = tool.execute({{"action", "list"}});
    std::string resultStr = result.dump();
    ASSERT_TRUE(resultStr.find("listed-skill") != std::string::npos);
    ASSERT_TRUE(resultStr.find("A listed skill") != std::string::npos);

    cleanupTmpDir();
}

void test_skill_tool_get_action() {
    setupTmpDir();
    std::string skillDir = tmpDir + "/skills";
    mkdir(skillDir.c_str(), 0755);

    writeFile(skillDir + "/test.md",
        "---\nname: get-skill\ndescription: Get me\nglobs: []\n---\n\nSkill body content here\n");

    SkillManager mgr;
    mgr.discoverFrom(skillDir);
    SkillTool tool(mgr);

    auto result = tool.execute({{"action", "get"}, {"name", "get-skill"}});
    std::string resultStr = result.dump();
    ASSERT_TRUE(resultStr.find("Skill body content here") != std::string::npos);

    cleanupTmpDir();
}

void test_skill_tool_get_nonexistent() {
    SkillManager mgr;
    SkillTool tool(mgr);
    auto result = tool.execute({{"action", "get"}, {"name", "nope"}});
    std::string resultStr = result.dump();
    ASSERT_TRUE(resultStr.find("error") != std::string::npos);
}

int main() {
    std::cout << "=== Skill System Tests ===\n";

    TEST(frontmatter_parsing);
    TEST(skill_body);
    TEST(skill_discovery_from_directory);
    TEST(invalid_skill_file_skipped);
    TEST(auto_activate_glob);
    TEST(auto_activate_no_match);
    TEST(activate_by_name);
    TEST(activate_nonexistent);
    TEST(active_skill_prompt);
    TEST(no_active_skills_empty_prompt);
    TEST(skill_tool_name);
    TEST(skill_tool_schema);
    TEST(skill_tool_list_action);
    TEST(skill_tool_get_action);
    TEST(skill_tool_get_nonexistent);

    std::cout << "\nResults: " << tests_passed << "/" << tests_run << " passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
