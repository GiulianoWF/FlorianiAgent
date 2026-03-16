#include "skills/host_skill_executor.h"
#include <cstdio>
#include <array>
#include <iostream>
#include <algorithm>
#include <cstring>

void HostSkillExecutor::set_allowed_commands(const std::vector<std::string>& allowed) {
    allowed_commands_ = std::set<std::string>(allowed.begin(), allowed.end());
}

void HostSkillExecutor::set_blocked_commands(const std::vector<std::string>& blocked) {
    blocked_commands_ = blocked;
}

void HostSkillExecutor::set_max_output_bytes(size_t max_bytes) {
    max_output_bytes_ = max_bytes;
}

void HostSkillExecutor::set_approval_callback(ApprovalCallback callback) {
    approval_callback_ = std::move(callback);
}

std::string HostSkillExecutor::shell_escape(const std::string& input) {
    // Safe shell escaping: wrap in single quotes and escape any embedded
    // single quotes using the pattern: ' -> '\''
    std::string escaped;
    escaped.reserve(input.size() + 10);
    for (char c : input) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    return "'" + escaped + "'";
}

std::string HostSkillExecutor::extract_base_command(const std::string& command) {
    // Trim leading whitespace
    auto start = command.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";

    // Handle env var prefixes like "VAR=val command" — skip them
    size_t pos = start;
    while (pos < command.size()) {
        auto space = command.find_first_of(" \t", pos);
        std::string token = (space == std::string::npos)
            ? command.substr(pos)
            : command.substr(pos, space - pos);

        // If token contains '=' and doesn't start with '-', it's a VAR=val prefix
        if (token.find('=') != std::string::npos && !token.empty() && token[0] != '-') {
            if (space == std::string::npos) return "";  // only env vars, no command
            pos = command.find_first_not_of(" \t", space);
            if (pos == std::string::npos) return "";
            continue;
        }

        // This is the actual command — extract basename
        // Handle paths like /usr/bin/ls -> ls
        auto slash = token.rfind('/');
        if (slash != std::string::npos) {
            token = token.substr(slash + 1);
        }
        return token;
    }
    return "";
}

std::vector<std::string> HostSkillExecutor::split_pipe_segments(const std::string& command) {
    std::vector<std::string> segments;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for (size_t i = 0; i < command.size(); i++) {
        char c = command[i];

        if (escaped) {
            current += c;
            escaped = false;
            continue;
        }

        if (c == '\\' && !in_single_quote) {
            current += c;
            escaped = true;
            continue;
        }

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            current += c;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            current += c;
            continue;
        }

        if (c == '|' && !in_single_quote && !in_double_quote) {
            segments.push_back(current);
            current.clear();
            continue;
        }

        current += c;
    }

    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

std::string HostSkillExecutor::detect_shell_injection(const std::string& command) {
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < command.size(); i++) {
        char c = command[i];

        // Track quoting state
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        // Only check metacharacters outside of single quotes
        // (single-quoted strings are literal in bash)
        if (in_single_quote) continue;

        // Command chaining
        if (c == ';') return "semicolon (;) command chaining";
        if (c == '&' && i + 1 < command.size() && command[i + 1] == '&')
            return "logical AND (&&) command chaining";
        if (c == '|' && i + 1 < command.size() && command[i + 1] == '|')
            return "logical OR (||) command chaining";

        // Command substitution
        if (c == '`') return "backtick command substitution";
        if (c == '$' && i + 1 < command.size() && command[i + 1] == '(')
            return "$() command substitution";

        // Output redirection
        if (c == '>' && (i == 0 || command[i - 1] != '\\'))
            return "output redirection (>)";

        // Process substitution
        if (c == '<' && i + 1 < command.size() && command[i + 1] == '(')
            return "<() process substitution";

        // Hex/octal escape sequences (used to encode commands)
        if (c == '$' && i + 1 < command.size() && command[i + 1] == '\'')
            return "$'...' ANSI-C quoting (can encode arbitrary bytes)";
    }

    return "";
}

bool HostSkillExecutor::is_blocked(const std::string& command) const {
    auto start = command.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return false;
    std::string trimmed = command.substr(start);

    for (const auto& pattern : blocked_commands_) {
        if (trimmed.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string HostSkillExecutor::validate_command(const std::string& command) const {
    // 1. Check for dangerous shell metacharacters
    auto injection = detect_shell_injection(command);
    if (!injection.empty()) {
        return "rejected: " + injection + " is not allowed";
    }

    // 2. Defense-in-depth: check blocklist
    if (is_blocked(command)) {
        return "rejected: command matches blocklist";
    }

    // 3. If allowlist is configured, validate each pipe segment
    if (!allowed_commands_.empty()) {
        auto segments = split_pipe_segments(command);
        for (const auto& segment : segments) {
            std::string base_cmd = extract_base_command(segment);
            if (base_cmd.empty()) {
                return "rejected: could not determine command in pipe segment";
            }
            if (allowed_commands_.find(base_cmd) == allowed_commands_.end()) {
                return "rejected: '" + base_cmd + "' is not in the allowed commands list";
            }
        }
    }

    return "";
}

SkillExecutor::Result HostSkillExecutor::execute(const SkillDef& skill,
                                                  const nlohmann::json& arguments) {
    Result result;

    std::string command;

    if (arguments.contains("command") && arguments["command"].is_string()) {
        std::string arg = arguments["command"].get<std::string>();

        auto error = validate_command(arg);
        if (!error.empty()) {
            result.output = "Error: " + error;
            result.exit_code = -1;
            std::cerr << "[SkillExecutor] " << error << ": " << arg << "\n";
            return result;
        }

        if (approval_callback_ && !approval_callback_(arg)) {
            result.output = "Error: command denied by user";
            result.exit_code = -1;
            std::cout << "[SkillExecutor] Command denied by user: " << arg << "\n";
            return result;
        }

        command = skill.executor_command + " " + shell_escape(arg);
    } else {
        std::string args_json = arguments.dump();

        if (approval_callback_ && !approval_callback_(skill.name + ": " + args_json)) {
            result.output = "Error: command denied by user";
            result.exit_code = -1;
            std::cout << "[SkillExecutor] Command denied by user: " << skill.name << "\n";
            return result;
        }

        command = "echo " + shell_escape(args_json) + " | " + skill.executor_command;
    }

    std::cout << "[SkillExecutor] Running: " << command << "\n";

    std::array<char, 4096> buffer;
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "Failed to execute command";
        result.exit_code = -1;
        return result;
    }

    bool truncated = false;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        if (max_output_bytes_ > 0 && output.size() + strlen(buffer.data()) > max_output_bytes_) {
            truncated = true;
            break;
        }
        output += buffer.data();
    }

    int status = pclose(pipe);
    result.exit_code = WEXITSTATUS(status);
    result.success = (result.exit_code == 0);
    result.output = output;

    if (truncated) {
        result.output += "\n... [output truncated at " +
                         std::to_string(max_output_bytes_) + " bytes]";
    }

    // Trim trailing newline
    if (!result.output.empty() && result.output.back() == '\n') {
        result.output.pop_back();
    }

    std::cout << "[SkillExecutor] Exit code: " << result.exit_code
              << ", output: " << result.output.substr(0, 200) << "\n";

    return result;
}
