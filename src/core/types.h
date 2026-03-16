#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments;  // JSON string
};

struct Message {
    std::string role;       // "system", "user", "assistant", "tool"
    std::string content;
    std::string tool_call_id;           // for role="tool" messages
    std::vector<ToolCall> tool_calls;   // for assistant messages with tool calls

    // Convert to OpenAI API JSON format
    nlohmann::json to_json() const {
        nlohmann::json j;
        j["role"] = role;

        if (!content.empty()) {
            j["content"] = content;
        }

        if (!tool_call_id.empty()) {
            j["tool_call_id"] = tool_call_id;
        }

        if (!tool_calls.empty()) {
            j["tool_calls"] = nlohmann::json::array();
            for (const auto& tc : tool_calls) {
                j["tool_calls"].push_back({
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", tc.arguments}
                    }}
                });
            }
        }

        return j;
    }
};

struct SkillDef {
    std::string name;
    std::string description;
    nlohmann::json parameter_schema;  // JSON Schema for parameters
    std::string executor_command;     // command to run
    std::string execution_env;       // "host" or future "docker"

    // Convert to OpenAI tools format
    nlohmann::json to_tool_json() const {
        return {
            {"type", "function"},
            {"function", {
                {"name", name},
                {"description", description},
                {"parameters", parameter_schema}
            }}
        };
    }

    // Load from a JSON file
    static SkillDef from_json(const nlohmann::json& j) {
        SkillDef s;
        s.name = j.at("name").get<std::string>();
        s.description = j.at("description").get<std::string>();
        s.parameter_schema = j.at("parameters");
        s.executor_command = j.at("executor").get<std::string>();
        s.execution_env = j.value("execution_env", "host");
        return s;
    }
};
