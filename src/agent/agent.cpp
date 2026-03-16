#include "agent/agent.h"
#include "storage/i_history_store.h"
#include "agent/monitor.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Built-in tool definition for spawn_agent
static SkillDef make_spawn_agent_tool() {
    SkillDef sd;
    sd.name = "spawn_agent";
    sd.description = "Spawn a sub-agent with a specific role and task. The sub-agent will "
                     "execute independently and return its result. Use this to delegate "
                     "subtasks like research, validation, or specialized processing.";
    sd.parameter_schema = {
        {"type", "object"},
        {"properties", {
            {"role", {{"type", "string"}, {"description", "The role of the sub-agent (e.g., 'researcher', 'validator', 'planner')"}}},
            {"system_prompt", {{"type", "string"}, {"description", "System prompt defining the sub-agent's behavior and constraints"}}},
            {"task", {{"type", "string"}, {"description", "The specific task for the sub-agent to complete"}}}
        }},
        {"required", json::array({"role", "task"})}
    };
    sd.executor_command = "__builtin__";
    sd.execution_env = "builtin";
    return sd;
}

Agent::Agent(ILlmClient& llm, SkillExecutor& skills,
             const std::string& system_prompt,
             const std::vector<SkillDef>& available_skills,
             int max_iterations)
    : llm_(llm), skills_(skills), agent_id_("main"),
      system_prompt_(system_prompt),
      available_skills_(available_skills), max_iterations_(max_iterations) {

    if (!system_prompt_.empty()) {
        messages_.push_back({"system", system_prompt_, "", {}});
    }
}

void Agent::set_history_store(IHistoryStore* store, const std::string& conversation_id) {
    history_store_ = store;
    conversation_id_ = conversation_id;
    load_history();
}

void Agent::set_monitor(Monitor* monitor) {
    monitor_ = monitor;
}

void Agent::persist(const Message& msg) {
    if (history_store_) {
        history_store_->add_message(conversation_id_, msg);
    }
}

void Agent::load_history() {
    if (!history_store_ || conversation_id_.empty()) return;

    auto history = history_store_->get_recent(conversation_id_, 20);
    if (history.empty()) return;

    std::cout << "[Agent:" << agent_id_ << "] Loaded " << history.size() << " messages from history\n";

    for (auto& msg : history) {
        messages_.push_back(std::move(msg));
    }
}

std::vector<SkillDef> Agent::get_all_tools() const {
    auto tools = available_skills_;
    tools.push_back(make_spawn_agent_tool());
    return tools;
}

std::string Agent::spawn_sub_agent(const std::string& role,
                                     const std::string& sub_system_prompt,
                                     const std::string& task) {
    std::string sub_id = agent_id_ + "/sub-" + std::to_string(next_sub_agent_id_++);

    std::string effective_prompt = sub_system_prompt.empty()
        ? "You are a " + role + " agent. Complete the assigned task thoroughly and concisely."
        : sub_system_prompt;

    std::cout << "[Agent:" << agent_id_ << "] Spawning sub-agent '" << sub_id
              << "' (role=" << role << ")\n";

    if (monitor_) {
        monitor_->register_agent(sub_id, role, task);
        monitor_->record_internal_message(agent_id_, sub_id, "Task: " + task);
    }

    // Create a sub-agent with its own context but shared LLM and skill executor.
    // Sub-agents get the same external skills but NOT spawn_agent (prevent infinite recursion).
    Agent sub_agent(llm_, skills_, effective_prompt, available_skills_, max_iterations_ / 2);
    sub_agent.agent_id_ = sub_id;
    sub_agent.monitor_ = monitor_;
    // Sub-agents don't persist to history (they're ephemeral)

    std::string result = sub_agent.run(task);

    if (monitor_) {
        monitor_->record_internal_message(sub_id, agent_id_, "Result: " + result.substr(0, 500));
        monitor_->update_agent_state(sub_id, "completed");
    }

    std::cout << "[Agent:" << agent_id_ << "] Sub-agent '" << sub_id
              << "' completed (" << result.size() << " chars)\n";

    return result;
}

std::string Agent::run(const std::string& user_message,
                       std::function<void(const std::string&)> on_token,
                       std::atomic<bool>* cancel) {

    if (monitor_) {
        monitor_->update_agent_state(agent_id_, "running");
    }

    Message user_msg{"user", user_message, "", {}};
    messages_.push_back(user_msg);
    persist(user_msg);

    auto all_tools = get_all_tools();

    for (int iteration = 0; iteration < max_iterations_; iteration++) {
        if (cancel && cancel->load()) {
            return "[cancelled]";
        }

        auto result = llm_.chat(messages_, all_tools, on_token, cancel);

        if (!result.success) {
            std::string error = "[LLM request failed]";
            Message err_msg{"assistant", error, "", {}};
            messages_.push_back(err_msg);
            persist(err_msg);
            return error;
        }

        if (!result.has_tool_calls()) {
            Message resp_msg{"assistant", result.content, "", {}};
            messages_.push_back(resp_msg);
            persist(resp_msg);
            return result.content;
        }

        std::cout << "[Agent:" << agent_id_ << "] LLM requested "
                  << result.tool_calls.size() << " tool call(s)\n";

        Message assistant_msg;
        assistant_msg.role = "assistant";
        assistant_msg.content = result.content;
        assistant_msg.tool_calls = result.tool_calls;
        messages_.push_back(assistant_msg);
        persist(assistant_msg);

        for (const auto& tc : result.tool_calls) {
            std::cout << "[Agent:" << agent_id_ << "] Executing tool: " << tc.name
                      << " (id=" << tc.id << ")\n";

            std::string tool_result;
            json args;

            try {
                args = json::parse(tc.arguments);
            } catch (const json::parse_error& e) {
                tool_result = "Error: invalid arguments JSON: " + std::string(e.what());
                std::cerr << "[Agent:" << agent_id_ << "] " << tool_result << "\n";
                Message err_msg{"tool", tool_result, tc.id, {}};
                messages_.push_back(err_msg);
                persist(err_msg);
                continue;
            }

            // Check for built-in spawn_agent tool
            if (tc.name == "spawn_agent") {
                std::string role = args.value("role", "assistant");
                std::string sub_prompt = args.value("system_prompt", "");
                std::string task = args.value("task", "");
                tool_result = spawn_sub_agent(role, sub_prompt, task);
            } else {
                // Find the matching external skill
                const SkillDef* skill = nullptr;
                for (const auto& s : available_skills_) {
                    if (s.name == tc.name) {
                        skill = &s;
                        break;
                    }
                }

                if (!skill) {
                    tool_result = "Error: unknown tool '" + tc.name + "'";
                    std::cerr << "[Agent:" << agent_id_ << "] " << tool_result << "\n";
                } else {
                    auto exec_result = skills_.execute(*skill, args);
                    tool_result = exec_result.output;

                    if (monitor_) {
                        monitor_->record_tool_execution(agent_id_, tc.name,
                            tc.arguments, tool_result, exec_result.exit_code);
                    }

                    if (!exec_result.success) {
                        tool_result = "Error (exit " + std::to_string(exec_result.exit_code)
                                      + "): " + exec_result.output;
                    }
                }
            }

            Message tool_msg{"tool", tool_result, tc.id, {}};
            messages_.push_back(tool_msg);
            persist(tool_msg);
        }
    }

    std::string error = "[max tool iterations reached]";
    Message err_msg{"assistant", error, "", {}};
    messages_.push_back(err_msg);
    persist(err_msg);
    return error;
}

std::vector<SkillDef> Agent::load_skills(const std::string& skills_dir) {
    std::vector<SkillDef> skills;

    if (!fs::exists(skills_dir) || !fs::is_directory(skills_dir)) {
        std::cout << "[Agent] Skills directory not found: " << skills_dir << "\n";
        return skills;
    }

    for (const auto& entry : fs::directory_iterator(skills_dir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream f(entry.path());
            json j = json::parse(f);
            skills.push_back(SkillDef::from_json(j));
            std::cout << "[Agent] Loaded skill: " << skills.back().name
                      << " from " << entry.path().filename() << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[Agent] Failed to load skill from "
                      << entry.path() << ": " << e.what() << "\n";
        }
    }

    return skills;
}
