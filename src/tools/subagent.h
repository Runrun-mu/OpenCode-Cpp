#pragma once
#include "tool.h"
#include "../llm/provider.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <map>

namespace opencodecpp {

class AgentLoop;

class SubagentTool : public Tool {
public:
    static constexpr int MAX_CONCURRENT = 3;
    static constexpr int MAX_DEPTH = 2;

    SubagentTool() = default;

    // Set the provider and available tools for creating subagent loops
    void setProvider(std::shared_ptr<LLMProvider> provider) { provider_ = provider; }
    void setAvailableTools(const std::map<std::string, std::shared_ptr<Tool>>& tools) { availableTools_ = tools; }
    void setDepth(int depth) { currentDepth_ = depth; }

    std::string name() const override { return "subagent"; }
    std::string description() const override {
        return "Launch a sub-agent to handle a complex task autonomously. "
               "The sub-agent has its own message history and can use specified tools.";
    }
    nlohmann::json schema() const override;
    nlohmann::json execute(const nlohmann::json& params) override;

    static std::atomic<int>& activeCount() {
        static std::atomic<int> count{0};
        return count;
    }

private:
    std::shared_ptr<LLMProvider> provider_;
    std::map<std::string, std::shared_ptr<Tool>> availableTools_;
    int currentDepth_ = 0;
};

} // namespace opencodecpp
