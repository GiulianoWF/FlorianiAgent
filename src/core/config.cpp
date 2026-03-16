#include "core/config.h"
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

Config Config::load(const std::string& config_dir) {
    Config cfg;
    cfg.config_dir = config_dir;
    cfg.skills_dir = config_dir + "/skills";
    cfg.history_db = config_dir + "/history.db";
    cfg.log_dir = config_dir + "/logs";

    std::string config_file = config_dir + "/config.yaml";

    if (!fs::exists(config_file)) {
        std::cout << "[Config] No config.yaml found, using defaults\n";
        // Set default pipeline
        cfg.pipeline_stages = {
            {"add_history", "builtin:add_history", "abort", ""},
            {"llm_agent_loop", "builtin:llm_agent_loop", "abort", ""},
            {"response", "builtin:response", "abort", ""}
        };
        return cfg;
    }

    try {
        YAML::Node root = YAML::LoadFile(config_file);

        // LLM section
        if (auto llm = root["llm"]) {
            if (llm["endpoint"]) cfg.llm_endpoint = llm["endpoint"].as<std::string>();
            if (llm["timeout_seconds"]) cfg.llm_timeout_seconds = llm["timeout_seconds"].as<int>();
        }

        // Server section
        if (auto server = root["server"]) {
            if (server["port"]) cfg.server_port = server["port"].as<int>();
        }

        // Agent section
        if (auto agent = root["agent"]) {
            if (agent["system_prompt"]) cfg.system_prompt = agent["system_prompt"].as<std::string>();
            if (agent["max_tool_iterations"]) cfg.max_tool_iterations = agent["max_tool_iterations"].as<int>();
        }

        // Pipeline section
        if (auto pipeline = root["pipeline"]) {
            if (auto stages = pipeline["stages"]) {
                cfg.pipeline_stages.clear();
                for (const auto& stage : stages) {
                    PipelineStageConfig s;
                    s.name = stage["name"].as<std::string>();
                    s.executor = stage["executor"].as<std::string>();
                    s.failure_policy = stage["failure_policy"].as<std::string>("abort");
                    if (stage["fallback_stage"]) {
                        s.fallback_stage = stage["fallback_stage"].as<std::string>();
                    }
                    cfg.pipeline_stages.push_back(s);
                }
            }
        }

        // If no pipeline stages defined in YAML, use defaults
        if (cfg.pipeline_stages.empty()) {
            cfg.pipeline_stages = {
                {"add_history", "builtin:add_history", "abort", ""},
                {"llm_agent_loop", "builtin:llm_agent_loop", "abort", ""},
                {"response", "builtin:response", "abort", ""}
            };
        }

        std::cout << "[Config] Loaded config from " << config_file << "\n";
    } catch (const YAML::Exception& e) {
        std::cerr << "[Config] Error parsing " << config_file << ": " << e.what()
                  << " — using defaults\n";
        cfg.pipeline_stages = {
            {"add_history", "builtin:add_history", "abort", ""},
            {"llm_agent_loop", "builtin:llm_agent_loop", "abort", ""},
            {"response", "builtin:response", "abort", ""}
        };
    }

    return cfg;
}

void Config::ensure_dirs() const {
    fs::create_directories(config_dir);
    fs::create_directories(skills_dir);
    fs::create_directories(log_dir);
}
