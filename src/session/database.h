#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace opencodecpp {

struct SessionRecord {
    std::string session_id;
    std::string created_at;
    std::string summary; // First 80 chars of last message
};

struct MessageRecord {
    std::string id;
    std::string session_id;
    std::string role;
    std::string content;
    std::string tool_calls;  // JSON string
    std::string tool_results; // JSON string
    std::string timestamp;
    int token_count = 0;
};

class Database {
public:
    Database();
    ~Database();

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    bool createTables();

    // Session operations
    std::string createSession();
    std::vector<SessionRecord> listSessions();
    bool sessionExists(const std::string& sessionId);

    // Message operations
    bool saveMessage(const MessageRecord& msg);
    std::vector<MessageRecord> getMessages(const std::string& sessionId);

private:
    sqlite3* db_ = nullptr;
    std::string generateUUID() const;
};

} // namespace opencodecpp
