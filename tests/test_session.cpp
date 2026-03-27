#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include "session/database.h"
#include "session/session.h"

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

static std::string testDbPath;

static void setupDb() {
    char tmpl[] = "/tmp/opencode_test_db_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (d) testDbPath = std::string(d) + "/test.db";
    else testDbPath = "/tmp/opencode_test_session.db";
}

static void cleanupDb() {
    unlink(testDbPath.c_str());
    // remove directory
    size_t pos = testDbPath.rfind('/');
    if (pos != std::string::npos) {
        std::string dir = testDbPath.substr(0, pos);
        rmdir(dir.c_str());
    }
}

// AC-9: Database opens and creates schema
void test_db_open_and_create_tables() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    ASSERT_TRUE(db.isOpen());
    db.close();
    ASSERT_FALSE(db.isOpen());
}

// AC-10: Creating a session generates UUID
void test_db_create_session() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    std::string id = db.createSession();
    ASSERT_FALSE(id.empty());
    // UUID format: 8-4-4-4-12
    ASSERT_TRUE(id.find('-') != std::string::npos);
    ASSERT_TRUE(db.sessionExists(id));
    db.close();
}

// AC-10: Multiple sessions have unique IDs
void test_db_unique_sessions() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    std::string id1 = db.createSession();
    std::string id2 = db.createSession();
    ASSERT_FALSE(id1.empty());
    ASSERT_FALSE(id2.empty());
    ASSERT_TRUE(id1 != id2);
    db.close();
}

// AC-11: Save and retrieve messages
void test_db_save_message() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    std::string sessionId = db.createSession();

    MessageRecord msg;
    msg.session_id = sessionId;
    msg.role = "user";
    msg.content = "Hello, world!";
    msg.token_count = 5;

    ASSERT_TRUE(db.saveMessage(msg));

    auto messages = db.getMessages(sessionId);
    ASSERT_TRUE(messages.size() >= 1);

    bool found = false;
    for (auto& m : messages) {
        if (m.content == "Hello, world!" && m.role == "user") {
            found = true;
            ASSERT_EQ(m.token_count, 5);
        }
    }
    ASSERT_TRUE(found);
    db.close();
}

// AC-11: Messages persist with role, content, token usage
void test_db_message_fields() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    std::string sessionId = db.createSession();

    MessageRecord msg;
    msg.session_id = sessionId;
    msg.role = "assistant";
    msg.content = "I can help with that.";
    msg.tool_calls = "[{\"name\":\"bash\",\"id\":\"123\"}]";
    msg.token_count = 42;

    ASSERT_TRUE(db.saveMessage(msg));

    auto messages = db.getMessages(sessionId);
    bool found = false;
    for (auto& m : messages) {
        if (m.role == "assistant" && m.content.find("help") != std::string::npos) {
            found = true;
            ASSERT_EQ(m.token_count, 42);
            ASSERT_TRUE(m.tool_calls.find("bash") != std::string::npos);
        }
    }
    ASSERT_TRUE(found);
    db.close();
}

// List sessions
void test_db_list_sessions() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    // We've created sessions in previous tests
    auto sessions = db.listSessions();
    ASSERT_TRUE(sessions.size() > 0);
    ASSERT_FALSE(sessions[0].session_id.empty());
    db.close();
}

// Session doesn't exist
void test_db_session_not_exists() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    ASSERT_FALSE(db.sessionExists("nonexistent-id-12345"));
    db.close();
}

// === SessionManager tests ===

// AC-12: SessionManager creates and loads sessions
void test_session_manager_create() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    std::string id = sm.createSession();
    ASSERT_FALSE(id.empty());
    ASSERT_EQ(sm.currentSessionId(), id);
}

void test_session_manager_add_and_get_history() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    std::string id = sm.createSession();

    Message msg;
    msg.role = "user";
    msg.content = "test message";
    sm.addMessage(msg, id, 10);

    auto history = sm.getHistory();
    ASSERT_TRUE(history.size() >= 1);
}

// AC-12: Load session restores history
void test_session_manager_load() {
    // Create a session with a message in one manager
    SessionManager sm1;
    ASSERT_TRUE(sm1.initialize(testDbPath));
    std::string id = sm1.createSession();

    Message msg;
    msg.role = "user";
    msg.content = "persistent message";
    sm1.addMessage(msg, id, 5);

    // Load in another manager
    SessionManager sm2;
    ASSERT_TRUE(sm2.initialize(testDbPath));
    ASSERT_TRUE(sm2.loadSession(id));
    ASSERT_EQ(sm2.currentSessionId(), id);
    auto history = sm2.getHistory();
    ASSERT_TRUE(history.size() >= 1);
}

void test_session_manager_load_nonexistent() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    ASSERT_FALSE(sm.loadSession("nonexistent-session-id"));
}

int main() {
    setupDb();

    std::cout << "=== Session Tests ===\n";

    // AC-9
    TEST(db_open_and_create_tables);

    // AC-10
    TEST(db_create_session);
    TEST(db_unique_sessions);

    // AC-11
    TEST(db_save_message);
    TEST(db_message_fields);
    TEST(db_list_sessions);
    TEST(db_session_not_exists);

    // AC-12
    TEST(session_manager_create);
    TEST(session_manager_add_and_get_history);
    TEST(session_manager_load);
    TEST(session_manager_load_nonexistent);

    cleanupDb();

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
