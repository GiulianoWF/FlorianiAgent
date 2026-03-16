#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

// Tracks internal system state for observability.
class Monitor {
public:
    struct AgentInfo {
        std::string id;
        std::string role;
        std::string state;  // "running", "completed", "failed"
        std::string goal;
    };

    struct ToolExecution {
        std::string agent_id;
        std::string tool_name;
        std::string arguments;
        std::string result;
        int exit_code;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct InternalMessage {
        std::string from_agent;
        std::string to_agent;
        std::string content;
        std::chrono::steady_clock::time_point timestamp;
    };

    // Agent tracking
    void register_agent(const std::string& id, const std::string& role, const std::string& goal);
    void update_agent_state(const std::string& id, const std::string& state);
    void remove_agent(const std::string& id);

    // Tool execution tracking
    void record_tool_execution(const std::string& agent_id, const std::string& tool_name,
                                const std::string& arguments, const std::string& result,
                                int exit_code);

    // Internal message tracking
    void record_internal_message(const std::string& from, const std::string& to,
                                  const std::string& content);

    // Pipeline state
    void set_pipeline_stage(const std::string& stage);
    void add_completed_stage(const std::string& stage);
    void reset_pipeline();

    // Get current state as JSON
    nlohmann::json to_json() const;

private:
    mutable std::mutex mutex_;
    std::vector<AgentInfo> agents_;
    std::vector<ToolExecution> recent_tool_executions_;  // last 50
    std::vector<InternalMessage> internal_messages_;      // last 100
    std::string current_pipeline_stage_;
    std::vector<std::string> completed_pipeline_stages_;
};
