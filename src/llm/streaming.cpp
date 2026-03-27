#include "streaming.h"

namespace opencodecpp {

SSEParser::SSEParser(SSEEventCallback onEvent, SSEDoneCallback onDone)
    : onEvent_(onEvent), onDone_(onDone) {}

void SSEParser::feed(const std::string& chunk) {
    if (done_) return;
    buffer_ += chunk;

    // Process complete lines
    size_t pos;
    while ((pos = buffer_.find('\n')) != std::string::npos) {
        std::string line = buffer_.substr(0, pos);
        buffer_ = buffer_.substr(pos + 1);

        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        processLine(line);
    }
}

void SSEParser::processLine(const std::string& line) {
    if (done_) return;

    if (line.empty()) {
        // Empty line = event boundary, but we process on data: lines
        return;
    }

    if (line.substr(0, 6) == "event:") {
        currentEvent_ = line.substr(6);
        // Trim leading space
        if (!currentEvent_.empty() && currentEvent_[0] == ' ')
            currentEvent_ = currentEvent_.substr(1);
        return;
    }

    if (line.substr(0, 5) == "data:") {
        std::string data = line.substr(5);
        // Trim leading space
        if (!data.empty() && data[0] == ' ')
            data = data.substr(1);

        if (data == "[DONE]") {
            done_ = true;
            if (onDone_) onDone_();
            return;
        }

        try {
            nlohmann::json j = nlohmann::json::parse(data);
            if (onEvent_) {
                onEvent_(currentEvent_, j);
            }
        } catch (...) {
            // Ignore malformed JSON
        }
    }
}

void SSEParser::reset() {
    buffer_.clear();
    currentEvent_.clear();
    done_ = false;
}

} // namespace opencodecpp
