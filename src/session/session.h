#pragma once
#include "database.h"
#include "../llm/provider.h"
#include <vector>
#include <memory>

namespace opencodecpp {

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    bool initialize(const std::string& dbPath);

    std::string createSession();
    std::vector<SessionRecord> listSessions();
    bool loadSession(const std::string& sessionId);

    void addMessage(const Message& msg, const std::string& sessionId, int tokenCount = 0);
    void clearAndReplace(const Message& summary);
    int messageCount() const { return static_cast<int>(history_.size()); }
    std::vector<Message> getHistory() const { return history_; }

    // Token estimation: (total_chars / 4) * 1.2
    double estimateTokens() const;

    // Get most recent messages fitting within tokenLimit, iterating from newest to oldest
    std::vector<Message> getRecentMessages(int tokenLimit) const;

    std::string currentSessionId() const { return currentSessionId_; }

private:
    Database db_;
    std::string currentSessionId_;
    std::vector<Message> history_;
};

} // namespace opencodecpp
