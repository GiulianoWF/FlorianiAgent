#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "core/types.h"
#include "llm-link/i_llm_client.h"
#include "skills/skill_executor.h"

class IHistoryStore;
class Monitor;

class Agent {
public:
    Agent(ILlmClient& llm, SkillExecutor& skills,
          const std::string& system_prompt,
          const std::vector<SkillDef>& available_skills,
          int max_iterations = 10);

    // Set the history store for persistence (optional).
    void set_history_store(IHistoryStore* store, const std::string& conversation_id);

    // Set the monitor for observability (optional).
    void set_monitor(Monitor* monitor);

    // Run the agentic loop for a user message.
    std::string run(const std::string& user_message,
                    std::function<void(const std::string&)> on_token = nullptr,
                    std::atomic<bool>* cancel = nullptr);

    // Access conversation history
    const std::vector<Message>& history() const { return messages_; }

    // Load skills from a directory of JSON files
    static std::vector<SkillDef> load_skills(const std::string& skills_dir);

private:
    ILlmClient& llm_;
    SkillExecutor& skills_;
    std::string agent_id_;
    std::string system_prompt_;
    std::vector<SkillDef> available_skills_;
    std::vector<Message> messages_;
    int max_iterations_;
    int next_sub_agent_id_ = 0;

    // Optional integrations
    IHistoryStore* history_store_ = nullptr;
    std::string conversation_id_;
    Monitor* monitor_ = nullptr;

    void persist(const Message& msg);
    void load_history();

    // Built-in tool: spawn a sub-agent
    std::string spawn_sub_agent(const std::string& role,
                                 const std::string& sub_system_prompt,
                                 const std::string& task);

    // Get all skills including built-in tools
    std::vector<SkillDef> get_all_tools() const;
};
