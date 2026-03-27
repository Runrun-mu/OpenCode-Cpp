#pragma once
#include <string>
#include <vector>
#include <functional>

namespace opencodecpp {

class InputHandler {
public:
    InputHandler();

    void addToHistory(const std::string& input);
    std::string getPreviousInput();
    std::string getNextInput();

    const std::vector<std::string>& history() const { return history_; }

private:
    std::vector<std::string> history_;
    int historyIndex_ = -1;
};

} // namespace opencodecpp
