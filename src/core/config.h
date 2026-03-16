#pragma once

#include <string>
#include <vector>

struct PipelineStageConfig {
    std::string name;
    std::string executor;        // "builtin:add_history", "builtin:llm_agent_loop", etc.
    std::string failure_policy;  // "abort", "skip", "retry_with_feedback", "fallback"
    std::string fallback_stage;  // used when failure_policy is "fallback"
};

struct Config {
    // LLM backend
    std::string llm_endpoint = "http://localhost:8001";
    int llm_timeout_seconds = 120;

    // Server
    int server_port = 8080;

    // Agent
    std::string system_prompt = "You are a helpful assistant. You have access to tools that you can use to help answer questions. Use them when appropriate.";
    int max_tool_iterations = 10;

    // Paths
    std::string config_dir;   // base config directory (~/.floriani-agent)
    std::string skills_dir;   // config_dir/skills
    std::string history_db;   // config_dir/history.db
    std::string log_dir;      // config_dir/logs

    // Pipeline stages
    std::vector<PipelineStageConfig> pipeline_stages;

    // Load config from a YAML file. Falls back to defaults if file doesn't exist.
    static Config load(const std::string& config_dir);

    // Ensure all directories exist
    void ensure_dirs() const;
};
