#include "session.h"

namespace opencodecpp {

SessionManager::SessionManager() {}
SessionManager::~SessionManager() {}

bool SessionManager::initialize(const std::string& dbPath) {
    return db_.open(dbPath);
}

std::string SessionManager::createSession() {
    currentSessionId_ = db_.createSession();
    history_.clear();
    return currentSessionId_;
}

std::vector<SessionRecord> SessionManager::listSessions() {
    return db_.listSessions();
}

bool SessionManager::loadSession(const std::string& sessionId) {
    if (!db_.sessionExists(sessionId)) return false;

    currentSessionId_ = sessionId;
    history_.clear();

    auto records = db_.getMessages(sessionId);
    for (auto& rec : records) {
        Message msg;
        msg.role = rec.role;
        msg.content = rec.content;
        if (!rec.tool_calls.empty()) {
            try {
                msg.tool_calls = nlohmann::json::parse(rec.tool_calls);
            } catch (...) {}
        }
        history_.push_back(msg);
    }

    return true;
}

void SessionManager::addMessage(const Message& msg, const std::string& sessionId, int tokenCount) {
    history_.push_back(msg);

    MessageRecord rec;
    rec.session_id = sessionId.empty() ? currentSessionId_ : sessionId;
    rec.role = msg.role;
    if (msg.content.is_string()) {
        rec.content = msg.content.get<std::string>();
    } else {
        rec.content = msg.content.dump();
    }
    if (!msg.tool_calls.is_null()) {
        rec.tool_calls = msg.tool_calls.dump();
    }
    rec.token_count = tokenCount;

    db_.saveMessage(rec);
}

} // namespace opencodecpp
