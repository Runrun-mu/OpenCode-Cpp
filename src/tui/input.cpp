#include "input.h"

namespace opencodecpp {

InputHandler::InputHandler() {}

void InputHandler::addToHistory(const std::string& input) {
    if (!input.empty()) {
        history_.push_back(input);
    }
    historyIndex_ = static_cast<int>(history_.size());
}

std::string InputHandler::getPreviousInput() {
    if (history_.empty()) return "";
    if (historyIndex_ > 0) historyIndex_--;
    return history_[historyIndex_];
}

std::string InputHandler::getNextInput() {
    if (history_.empty()) return "";
    if (historyIndex_ < static_cast<int>(history_.size()) - 1) {
        historyIndex_++;
        return history_[historyIndex_];
    }
    historyIndex_ = static_cast<int>(history_.size());
    return "";
}

} // namespace opencodecpp
