#include "database.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace opencodecpp {

Database::Database() {}

Database::~Database() {
    close();
}

bool Database::open(const std::string& path) {
    if (db_) close();
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        db_ = nullptr;
        return false;
    }
    return createTables();
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::createTables() {
    const char* sessionsSql =
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  session_id TEXT PRIMARY KEY,"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ");";

    const char* messagesSql =
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

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sessionsSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    rc = sqlite3_exec(db_, messagesSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    return true;
}

std::string Database::generateUUID() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << dis(gen) << "-";
    ss << std::setw(4) << (dis(gen) & 0xFFFF) << "-";
    ss << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000) << "-";
    ss << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000) << "-";
    ss << std::setw(8) << dis(gen);
    ss << std::setw(4) << (dis(gen) & 0xFFFF);
    return ss.str();
}

std::string Database::createSession() {
    std::string id = generateUUID();
    const char* sql = "INSERT INTO sessions (session_id) VALUES (?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? id : "";
}

std::vector<SessionRecord> Database::listSessions() {
    std::vector<SessionRecord> sessions;
    const char* sql =
        "SELECT s.session_id, s.created_at, "
        "COALESCE(SUBSTR(m.content, 1, 80), '') as summary "
        "FROM sessions s "
        "LEFT JOIN messages m ON m.session_id = s.session_id "
        "AND m.timestamp = (SELECT MAX(m2.timestamp) FROM messages m2 WHERE m2.session_id = s.session_id) "
        "ORDER BY s.created_at DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return sessions;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionRecord rec;
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.summary = summary ? summary : "";
        sessions.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return sessions;
}

bool Database::sessionExists(const std::string& sessionId) {
    const char* sql = "SELECT COUNT(*) FROM sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return exists;
}

bool Database::saveMessage(const MessageRecord& msg) {
    const char* sql =
        "INSERT INTO messages (id, session_id, role, content, tool_calls, tool_results, token_count) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string id = msg.id.empty() ? generateUUID() : msg.id;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, msg.session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, msg.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, msg.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, msg.tool_calls.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, msg.tool_results.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, msg.token_count);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<MessageRecord> Database::getMessages(const std::string& sessionId) {
    std::vector<MessageRecord> messages;
    const char* sql =
        "SELECT id, session_id, role, content, tool_calls, tool_results, timestamp, token_count "
        "FROM messages WHERE session_id = ? ORDER BY timestamp ASC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return messages;
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageRecord rec;
        rec.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        rec.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rec.content = content ? content : "";
        const char* tc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.tool_calls = tc ? tc : "";
        const char* tr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        rec.tool_results = tr ? tr : "";
        const char* ts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        rec.timestamp = ts ? ts : "";
        rec.token_count = sqlite3_column_int(stmt, 7);
        messages.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return messages;
}

} // namespace opencodecpp
