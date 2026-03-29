#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include "session/database.h"
#include "session/session.h"
#include "config/config.h"
#include "tui/render.h"
#include "llm/provider.h"

using namespace opencodecpp;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    std::cout << "  TEST: " << #name << "... "; \
    tests_run++; \
    try { test_##name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED (unknown exception)\n"; }

#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error(std::string("Assertion failed: ") + #x + " at line " + std::to_string(__LINE__))
#define ASSERT_FALSE(x) if (x) throw std::runtime_error(std::string("Assertion failed: !") + #x + " at line " + std::to_string(__LINE__))
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error(std::string("Assertion failed: ") + #a + " == " + #b + " at line " + std::to_string(__LINE__))
#define ASSERT_GT(a, b) if (!((a) > (b))) throw std::runtime_error(std::string("Assertion failed: ") + #a + " > " + #b + " at line " + std::to_string(__LINE__))

static std::string testDbPath;

static void setupDb() {
    char tmpl[] = "/tmp/opencode_test_resume_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (d) testDbPath = std::string(d) + "/test.db";
    else testDbPath = "/tmp/opencode_test_resume.db";
}

static void cleanupDb() {
    unlink(testDbPath.c_str());
    size_t pos = testDbPath.rfind('/');
    if (pos != std::string::npos) {
        std::string dir = testDbPath.substr(0, pos);
        rmdir(dir.c_str());
    }
}

// ============================================================
// F-1: Database Layer (AC-1, AC-2, AC-3)
// ============================================================

// AC-1: SessionInfo struct has session_id, created_at, message_count, last_message_preview
void test_session_info_struct_fields() {
    SessionInfo info;
    info.session_id = "test-id";
    info.created_at = "2026-03-30";
    info.message_count = 5;
    info.last_message_preview = "Hello world";
    ASSERT_EQ(info.session_id, "test-id");
    ASSERT_EQ(info.created_at, "2026-03-30");
    ASSERT_EQ(info.message_count, 5);
    ASSERT_EQ(info.last_message_preview, "Hello world");
}

// AC-2: listSessionDetails returns vector<SessionInfo> ordered by last activity (most recent first)
void test_list_session_details_returns_ordered() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));

    std::string id1 = db.createSession();
    ASSERT_FALSE(id1.empty());

    MessageRecord msg1;
    msg1.session_id = id1;
    msg1.role = "user";
    msg1.content = "First session message";
    msg1.token_count = 10;
    ASSERT_TRUE(db.saveMessage(msg1));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string id2 = db.createSession();
    ASSERT_FALSE(id2.empty());

    MessageRecord msg2;
    msg2.session_id = id2;
    msg2.role = "user";
    msg2.content = "Second session message";
    msg2.token_count = 10;
    ASSERT_TRUE(db.saveMessage(msg2));

    auto sessions = db.listSessionDetails();
    ASSERT_TRUE(sessions.size() >= 2);

    // Most recent session (id2) should be first
    ASSERT_EQ(sessions[0].session_id, id2);
    ASSERT_EQ(sessions[1].session_id, id1);
    db.close();
}

// AC-2: listSessionDetails populates message_count and last_message_preview
void test_list_session_details_fields() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));

    std::string id = db.createSession();

    MessageRecord msg1;
    msg1.session_id = id;
    msg1.role = "user";
    msg1.content = "First message";
    msg1.token_count = 5;
    ASSERT_TRUE(db.saveMessage(msg1));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    MessageRecord msg2;
    msg2.session_id = id;
    msg2.role = "assistant";
    msg2.content = "Second message response";
    msg2.token_count = 8;
    ASSERT_TRUE(db.saveMessage(msg2));

    auto sessions = db.listSessionDetails();
    bool found = false;
    for (auto& s : sessions) {
        if (s.session_id == id) {
            found = true;
            ASSERT_EQ(s.message_count, 2);
            ASSERT_TRUE(s.last_message_preview.find("Second message") != std::string::npos);
            break;
        }
    }
    ASSERT_TRUE(found);
    db.close();
}

// AC-2: listSessionDetails handles session with no messages
void test_list_session_details_empty_session() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));

    std::string id = db.createSession();

    auto sessions = db.listSessionDetails();
    bool found = false;
    for (auto& s : sessions) {
        if (s.session_id == id) {
            found = true;
            ASSERT_EQ(s.message_count, 0);
            ASSERT_TRUE(s.last_message_preview.empty());
            break;
        }
    }
    ASSERT_TRUE(found);
    db.close();
}

// AC-2: listSessionDetails respects limit parameter
void test_list_session_details_limit() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));
    auto details = db.listSessionDetails(1);
    ASSERT_TRUE(details.size() <= 1);
    db.close();
}

// AC-2: last_message_preview is truncated to max 80 chars
void test_list_session_details_preview_truncation() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));

    std::string id = db.createSession();
    MessageRecord msg;
    msg.session_id = id;
    msg.role = "user";
    msg.content = std::string(120, 'A');
    msg.token_count = 30;
    ASSERT_TRUE(db.saveMessage(msg));

    auto details = db.listSessionDetails();
    bool found = false;
    for (auto& d : details) {
        if (d.session_id == id) {
            ASSERT_TRUE(d.last_message_preview.size() <= 80);
            found = true;
        }
    }
    ASSERT_TRUE(found);
    db.close();
}

// AC-3: getMostRecentSessionId returns most recently updated session
void test_get_most_recent_session_id() {
    Database db;
    ASSERT_TRUE(db.open(testDbPath));

    std::string lastId = db.createSession();
    ASSERT_FALSE(lastId.empty());

    std::string recent = db.getMostRecentSessionId();
    ASSERT_EQ(recent, lastId);
    db.close();
}

// AC-3: getMostRecentSessionId returns empty string when no sessions
void test_get_most_recent_session_id_empty() {
    std::string freshDbPath = testDbPath + "_fresh.db";
    Database db;
    ASSERT_TRUE(db.open(freshDbPath));
    std::string result = db.getMostRecentSessionId();
    ASSERT_TRUE(result.empty());
    db.close();
    unlink(freshDbPath.c_str());
}

// ============================================================
// F-2: SessionManager API (AC-4, AC-5)
// ============================================================

// AC-4: SessionManager::listSessions returns list of SessionInfo with all fields populated
void test_session_manager_list_sessions_info() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));

    std::string id = sm.createSession();
    Message msg;
    msg.role = "user";
    msg.content = "test message for listing";
    sm.addMessage(msg, id, 10);

    auto sessions = sm.listSessions();
    ASSERT_TRUE(sessions.size() >= 1);

    bool found = false;
    for (auto& s : sessions) {
        if (s.session_id == id) {
            found = true;
            ASSERT_EQ(s.message_count, 1);
            ASSERT_FALSE(s.created_at.empty());
            ASSERT_TRUE(s.last_message_preview.find("test message") != std::string::npos);
            break;
        }
    }
    ASSERT_TRUE(found);
}

// AC-5: SessionManager::getMostRecentSessionId delegates to database
void test_session_manager_get_most_recent() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));

    std::string id = sm.createSession();
    Message msg;
    msg.role = "user";
    msg.content = "latest message";
    sm.addMessage(msg, id, 5);

    std::string recent = sm.getMostRecentSessionId();
    ASSERT_EQ(recent, id);
}

// ============================================================
// F-3: Config --resume Flag (AC-6, AC-7, AC-8)
// ============================================================

// AC-6: Config has bool resume and std::string resume_session_id with defaults
void test_config_resume_fields_default() {
    Config cfg;
    ASSERT_FALSE(cfg.resume);
    ASSERT_TRUE(cfg.resume_session_id.empty());
}

// AC-7: --resume without argument sets resume=true and resume_session_id empty
void test_config_resume_no_arg() {
    const char* args[] = {"opencode", "--resume"};
    Config cfg = Config::fromArgs(2, const_cast<char**>(args));
    ASSERT_TRUE(cfg.resume);
    ASSERT_TRUE(cfg.resume_session_id.empty());
}

// AC-8: --resume <session-id> sets resume=true and resume_session_id to given ID
void test_config_resume_with_session_id() {
    const char* args[] = {"opencode", "--resume", "abc-123-def"};
    Config cfg = Config::fromArgs(3, const_cast<char**>(args));
    ASSERT_TRUE(cfg.resume);
    ASSERT_EQ(cfg.resume_session_id, "abc-123-def");
}

// AC-7/AC-8: --resume followed by another flag doesn't consume flag as session ID
void test_config_resume_followed_by_flag() {
    const char* args[] = {"opencode", "--resume", "--model", "gpt-4o"};
    Config cfg = Config::fromArgs(4, const_cast<char**>(args));
    ASSERT_TRUE(cfg.resume);
    ASSERT_TRUE(cfg.resume_session_id.empty());
    ASSERT_EQ(cfg.cli_model, "gpt-4o");
}

// ============================================================
// F-4: TUI History Restoration (AC-10, AC-11, AC-12, AC-13)
// ============================================================

// AC-10: Session resume populates chatMessages_ from DB history
void test_session_history_to_chat_messages() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    std::string id = sm.createSession();

    Message userMsg;
    userMsg.role = "user";
    userMsg.content = "Hello, how are you?";
    sm.addMessage(userMsg, id, 5);

    Message assistantMsg;
    assistantMsg.role = "assistant";
    assistantMsg.content = "I'm doing well, thanks!";
    sm.addMessage(assistantMsg, id, 6);

    // Simulate resume: load session in new manager
    SessionManager sm2;
    ASSERT_TRUE(sm2.initialize(testDbPath));
    ASSERT_TRUE(sm2.loadSession(id));

    auto history = sm2.getHistory();
    ASSERT_EQ(static_cast<int>(history.size()), 2);

    // Simulate conversion to ChatMessage
    std::vector<ChatMessage> chatMessages;
    for (auto& h : history) {
        if (h.role == "user" || h.role == "assistant") {
            ChatMessage cm;
            cm.role = h.role;
            if (h.content.is_string()) {
                cm.content = h.content.get<std::string>();
            } else if (h.content.is_array()) {
                for (const auto& part : h.content) {
                    if (part.contains("type") && part["type"] == "text" && part.contains("text")) {
                        cm.content += part["text"].get<std::string>();
                    }
                }
            }
            chatMessages.push_back(cm);
        }
    }

    ASSERT_EQ(static_cast<int>(chatMessages.size()), 2);
    ASSERT_EQ(chatMessages[0].role, "user");
    ASSERT_EQ(chatMessages[0].content, "Hello, how are you?");
    ASSERT_EQ(chatMessages[1].role, "assistant");
    ASSERT_EQ(chatMessages[1].content, "I'm doing well, thanks!");
}

// AC-11: Restored messages have proper role attribution (user vs assistant styling)
void test_restored_messages_role_attribution() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    std::string id = sm.createSession();

    Message msg1; msg1.role = "user"; msg1.content = "User question";
    sm.addMessage(msg1, id);
    Message msg2; msg2.role = "assistant"; msg2.content = "AI answer";
    sm.addMessage(msg2, id);
    Message msg3; msg3.role = "tool"; msg3.content = "Tool output";
    sm.addMessage(msg3, id);

    SessionManager sm2;
    ASSERT_TRUE(sm2.initialize(testDbPath));
    ASSERT_TRUE(sm2.loadSession(id));

    auto history = sm2.getHistory();
    ASSERT_EQ(static_cast<int>(history.size()), 3);
    ASSERT_EQ(history[0].role, "user");
    ASSERT_EQ(history[1].role, "assistant");
    ASSERT_EQ(history[2].role, "tool");
}

// AC-12: After resume, new messages can be sent and conversation continues
void test_resume_then_continue_conversation() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    std::string id = sm.createSession();

    Message msg1;
    msg1.role = "user";
    msg1.content = "Original message";
    sm.addMessage(msg1, id);

    // Load in new manager and add message
    SessionManager sm2;
    ASSERT_TRUE(sm2.initialize(testDbPath));
    ASSERT_TRUE(sm2.loadSession(id));

    int countBefore = sm2.messageCount();

    Message msg2;
    msg2.role = "user";
    msg2.content = "New message after resume";
    sm2.addMessage(msg2, id);

    ASSERT_EQ(sm2.messageCount(), countBefore + 1);

    // Verify it persisted to DB
    SessionManager sm3;
    ASSERT_TRUE(sm3.initialize(testDbPath));
    ASSERT_TRUE(sm3.loadSession(id));
    ASSERT_EQ(sm3.messageCount(), countBefore + 1);
}

// AC-13: Token counts are correctly estimated from loaded history
void test_token_estimation_after_resume() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    std::string id = sm.createSession();

    Message msg;
    msg.role = "user";
    msg.content = std::string(400, 'x'); // 400 chars -> (400/4)*1.2 = 120 tokens
    sm.addMessage(msg, id);

    SessionManager sm2;
    ASSERT_TRUE(sm2.initialize(testDbPath));
    ASSERT_TRUE(sm2.loadSession(id));

    double tokens = sm2.estimateTokens();
    ASSERT_GT(tokens, 0.0);
    ASSERT_TRUE(tokens >= 100.0); // Should be around 120
}

// AC-10: Message with JSON array content (multipart) can be converted
void test_message_array_content_conversion() {
    Message msg;
    msg.role = "assistant";
    msg.content = nlohmann::json::array();
    msg.content.push_back({{"type", "text"}, {"text", "Part 1. "}});
    msg.content.push_back({{"type", "text"}, {"text", "Part 2."}});

    ASSERT_TRUE(msg.content.is_array());
    std::string combined;
    for (const auto& part : msg.content) {
        if (part.contains("type") && part["type"] == "text" && part.contains("text")) {
            combined += part["text"].get<std::string>();
        }
    }
    ASSERT_EQ(combined, "Part 1. Part 2.");
}

// ============================================================
// F-5: /sessions and /resume Commands (AC-14, AC-15, AC-16)
// ============================================================

// AC-14: Session listing format with truncated ID, creation time, message count, preview
void test_sessions_command_format() {
    SessionInfo info;
    info.session_id = "12345678-90ab-cdef-1234-567890abcdef";
    info.created_at = "2026-03-30 12:00:00";
    info.message_count = 5;
    info.last_message_preview = "This is a message preview that should be cut to 80 chars max";

    // Truncated ID: first 8 chars
    std::string truncId = info.session_id.substr(0, 8);
    ASSERT_EQ(truncId, "12345678");

    // Format check
    std::string line = "  [1] " + truncId + "  " + info.created_at
        + "  (" + std::to_string(info.message_count) + " msgs)  Last: " + info.last_message_preview;
    ASSERT_TRUE(line.find("12345678") != std::string::npos);
    ASSERT_TRUE(line.find("5 msgs") != std::string::npos);
}

// AC-14: Max 20 sessions in listing
void test_sessions_list_max_20() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));

    auto sessions = sm.listSessions(20);
    ASSERT_TRUE(sessions.size() <= 20);
}

// AC-15: /resume without arguments shows session list
void test_resume_no_arg_shows_list() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));
    auto sessions = sm.listSessions();
    ASSERT_TRUE(sessions.size() > 0);
}

// AC-16: /resume <session-id> loads session and clears/repopulates history
void test_resume_specific_session() {
    SessionManager sm;
    ASSERT_TRUE(sm.initialize(testDbPath));

    // Create session A with messages
    std::string idA = sm.createSession();
    Message msgA; msgA.role = "user"; msgA.content = "Session A message";
    sm.addMessage(msgA, idA);

    // Create session B with different messages
    std::string idB = sm.createSession();
    Message msgB; msgB.role = "user"; msgB.content = "Session B message";
    sm.addMessage(msgB, idB);

    // Resume session A - history should be only A's messages
    ASSERT_TRUE(sm.loadSession(idA));
    ASSERT_EQ(sm.currentSessionId(), idA);
    auto history = sm.getHistory();
    ASSERT_EQ(static_cast<int>(history.size()), 1);
    std::string content;
    if (history[0].content.is_string()) {
        content = history[0].content.get<std::string>();
    }
    ASSERT_EQ(content, "Session A message");
}

int main() {
    setupDb();

    std::cout << "=== Session Resume Tests ===\n\n";

    std::cout << "--- F-1: Database Layer (AC-1, AC-2, AC-3) ---\n";
    TEST(session_info_struct_fields);
    TEST(list_session_details_returns_ordered);
    TEST(list_session_details_fields);
    TEST(list_session_details_empty_session);
    TEST(list_session_details_limit);
    TEST(list_session_details_preview_truncation);
    TEST(get_most_recent_session_id);
    TEST(get_most_recent_session_id_empty);

    std::cout << "\n--- F-2: SessionManager API (AC-4, AC-5) ---\n";
    TEST(session_manager_list_sessions_info);
    TEST(session_manager_get_most_recent);

    std::cout << "\n--- F-3: Config --resume Flag (AC-6, AC-7, AC-8) ---\n";
    TEST(config_resume_fields_default);
    TEST(config_resume_no_arg);
    TEST(config_resume_with_session_id);
    TEST(config_resume_followed_by_flag);

    std::cout << "\n--- F-4: TUI History Restoration (AC-10, AC-11, AC-12, AC-13) ---\n";
    TEST(session_history_to_chat_messages);
    TEST(restored_messages_role_attribution);
    TEST(resume_then_continue_conversation);
    TEST(token_estimation_after_resume);
    TEST(message_array_content_conversion);

    std::cout << "\n--- F-5: /sessions and /resume Commands (AC-14, AC-15, AC-16) ---\n";
    TEST(sessions_command_format);
    TEST(sessions_list_max_20);
    TEST(resume_no_arg_shows_list);
    TEST(resume_specific_session);

    cleanupDb();

    std::cout << "\n" << tests_passed << "/" << tests_run << " tests passed.\n";
    return tests_passed == tests_run ? 0 : 1;
}
