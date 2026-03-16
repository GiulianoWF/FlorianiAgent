#include "agent/monitor.h"

using json = nlohmann::json;

void Monitor::register_agent(const std::string& id, const std::string& role,
                              const std::string& goal) {
    std::lock_guard<std::mutex> lock(mutex_);
    agents_.push_back({id, role, "running", goal});
}

void Monitor::update_agent_state(const std::string& id, const std::string& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& a : agents_) {
        if (a.id == id) {
            a.state = state;
            return;
        }
    }
}

void Monitor::remove_agent(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    agents_.erase(
        std::remove_if(agents_.begin(), agents_.end(),
                        [&id](const AgentInfo& a) { return a.id == id; }),
        agents_.end());
}

void Monitor::record_tool_execution(const std::string& agent_id, const std::string& tool_name,
                                     const std::string& arguments, const std::string& result,
                                     int exit_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    recent_tool_executions_.push_back({
        agent_id, tool_name, arguments, result, exit_code,
        std::chrono::steady_clock::now()
    });
    // Keep only last 50
    if (recent_tool_executions_.size() > 50) {
        recent_tool_executions_.erase(recent_tool_executions_.begin());
    }
}

void Monitor::record_internal_message(const std::string& from, const std::string& to,
                                       const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    internal_messages_.push_back({
        from, to, content,
        std::chrono::steady_clock::now()
    });
    // Keep only last 100
    if (internal_messages_.size() > 100) {
        internal_messages_.erase(internal_messages_.begin());
    }
}

void Monitor::set_pipeline_stage(const std::string& stage) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_pipeline_stage_ = stage;
}

void Monitor::add_completed_stage(const std::string& stage) {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_pipeline_stages_.push_back(stage);
}

void Monitor::reset_pipeline() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_pipeline_stage_.clear();
    completed_pipeline_stages_.clear();
}

json Monitor::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);

    json agents = json::array();
    for (const auto& a : agents_) {
        agents.push_back({
            {"id", a.id},
            {"role", a.role},
            {"state", a.state},
            {"goal", a.goal}
        });
    }

    json tools = json::array();
    for (const auto& t : recent_tool_executions_) {
        tools.push_back({
            {"agent_id", t.agent_id},
            {"tool_name", t.tool_name},
            {"arguments", t.arguments},
            {"result", t.result.substr(0, 500)},
            {"exit_code", t.exit_code}
        });
    }

    json messages = json::array();
    for (const auto& m : internal_messages_) {
        messages.push_back({
            {"from", m.from_agent},
            {"to", m.to_agent},
            {"content", m.content.substr(0, 500)}
        });
    }

    return {
        {"active_agents", agents},
        {"pipeline", {
            {"current_stage", current_pipeline_stage_},
            {"stages_completed", completed_pipeline_stages_}
        }},
        {"recent_tool_executions", tools},
        {"internal_messages", messages}
    };
}
