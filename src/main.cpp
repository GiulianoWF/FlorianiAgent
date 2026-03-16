#include "inbound-server/http_server.h"
#include "llm-link/llm_client.h"
#include "agent/agent.h"
#include "skills/host_skill_executor.h"
#include "core/config.h"
#include "storage/history_store.h"
#include "agent/pipeline.h"
#include "agent/monitor.h"
#include <iostream>
#include <string>
#include <cstdlib>

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [OPTIONS]\n"
              << "  --port PORT        Listen port (overrides config)\n"
              << "  --backend URL      LLM backend URL (overrides config)\n"
              << "  --config-dir DIR   Config directory (default: ~/.floriani-agent)\n"
              << "  --new-conversation Start a new conversation\n"
              << "  --approve-commands Require manual approval for every command\n"
              << "  --help             Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string config_dir;
    int port_override = 0;
    std::string backend_override;
    bool new_conversation = false;
    bool approve_commands = false;

    // Default config dir: ~/.floriani-agent
    if (const char* home = std::getenv("HOME")) {
        config_dir = std::string(home) + "/.floriani-agent";
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port_override = std::atoi(argv[++i]);
        } else if (arg == "--backend" && i + 1 < argc) {
            backend_override = argv[++i];
        } else if (arg == "--config-dir" && i + 1 < argc) {
            config_dir = argv[++i];
        } else if (arg == "--new-conversation") {
            new_conversation = true;
        } else if (arg == "--approve-commands") {
            approve_commands = true;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Load config
    Config cfg = Config::load(config_dir);
    cfg.ensure_dirs();

    // CLI overrides
    if (port_override > 0) cfg.server_port = port_override;
    if (!backend_override.empty()) cfg.llm_endpoint = backend_override;

    std::cout << "[floriani-agent] Config dir: " << cfg.config_dir << "\n";
    std::cout << "[floriani-agent] Backend: " << cfg.llm_endpoint << "\n";
    std::cout << "[floriani-agent] Pipeline: " << cfg.pipeline_stages.size() << " stage(s)\n";

    // Load skills
    auto skills = Agent::load_skills(cfg.skills_dir);
    std::cout << "[floriani-agent] Loaded " << skills.size() << " skill(s)\n";

    // Initialize history store
    HistoryStore history(cfg.history_db);
    std::string conv_id;
    if (new_conversation) {
        conv_id = history.create_conversation();
        std::cout << "[floriani-agent] New conversation: " << conv_id << "\n";
    } else {
        conv_id = history.get_or_create_conversation();
        std::cout << "[floriani-agent] Conversation: " << conv_id << "\n";
    }

    // Initialize components
    LlmClient llm_client(cfg.llm_endpoint);
    HostSkillExecutor skill_executor;
    // TODO: make this good.
    // Primary defense: only these commands can run on the host.
    // Pipes between allowed commands are permitted (e.g., "ls | grep foo").
    skill_executor.set_allowed_commands({
        "ls", "cat", "head", "tail", "wc", "sort", "uniq", "cut", "tr",
        "grep", "egrep", "fgrep", "rg",
        "find", "file", "stat", "readlink", "realpath", "basename", "dirname",
        "tree", "du", "df",
        "echo", "printf", "date", "pwd", "whoami", "hostname", "uname",
        "ps", "uptime", "free",
        "diff", "comm", "md5sum", "sha256sum",
        "jq", "sed", "awk",
        "xargs",
        "bazel",
        "git",
        "python3", "python",
    });
    // Defense-in-depth: blocklist catches anything that slips through.
    skill_executor.set_blocked_commands({
        "rm -rf /",
        "mkfs",
        "dd if=",
    });
    if (approve_commands) {
        std::cout << "[floriani-agent] Manual command approval ENABLED\n";
        skill_executor.set_approval_callback([](const std::string& command) -> bool {
            std::cout << "\n\033[1;33m[APPROVE?]\033[0m " << command << "\n"
                      << "  Allow this command? [y/N]: ";
            std::string input;
            if (!std::getline(std::cin, input)) return false;
            return (!input.empty() && (input[0] == 'y' || input[0] == 'Y'));
        });
    }

    Monitor monitor;
    Agent agent(llm_client, skill_executor, cfg.system_prompt, skills, cfg.max_tool_iterations);
    agent.set_history_store(&history, conv_id);
    agent.set_monitor(&monitor);
    monitor.register_agent("main", "main", "Handle user requests");

    // Build pipeline with built-in stage executors
    Pipeline pipeline(cfg.pipeline_stages);

    pipeline.register_builtin("add_history", [](PipelineContext& ctx) -> StageResult {
        // History is already managed by the Agent's set_history_store.
        // This stage is a hook point for future expansion.
        return {true, ""};
    });

    pipeline.register_builtin("llm_agent_loop", [](PipelineContext& ctx) -> StageResult {
        if (!ctx.agent) {
            return {false, "No agent configured"};
        }
        ctx.response_text = ctx.agent->run(ctx.user_message, ctx.on_token, ctx.cancel);
        if (ctx.response_text.empty() || ctx.response_text.rfind("[", 0) == 0) {
            // Check for error markers like [LLM request failed]
            if (ctx.response_text.find("failed") != std::string::npos ||
                ctx.response_text.find("error") != std::string::npos) {
                return {false, ctx.response_text};
            }
        }
        return {true, ""};
    });

    pipeline.register_builtin("response", [](PipelineContext& ctx) -> StageResult {
        // Response formatting is handled by the HTTP server.
        // This stage is a hook point for post-processing.
        return {true, ""};
    });

    HttpServer server(cfg.server_port, llm_client, agent, pipeline, history, monitor, conv_id);
    server.start();

    return 0;
}
