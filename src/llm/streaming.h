#pragma once
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace opencodecpp {

using SSEEventCallback = std::function<void(const std::string& event, const nlohmann::json& data)>;
using SSEDoneCallback = std::function<void()>;

class SSEParser {
public:
    SSEParser(SSEEventCallback onEvent, SSEDoneCallback onDone);

    // Feed raw data chunks from HTTP response
    void feed(const std::string& chunk);

    // Reset parser state
    void reset();

    bool isDone() const { return done_; }

private:
    void processLine(const std::string& line);

    SSEEventCallback onEvent_;
    SSEDoneCallback onDone_;
    std::string buffer_;
    std::string currentEvent_;
    bool done_ = false;
};

} // namespace opencodecpp
