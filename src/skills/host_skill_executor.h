#pragma once

#include <string>
#include <vector>
#include <set>
#include <functional>
#include "skills/skill_executor.h"

// Callback for command approval. Receives the command string, returns true to allow.
using ApprovalCallback = std::function<bool(const std::string& command)>;

// Executes skills as local host processes via popen().
//
// Security model:
//   - Host execution uses an ALLOWLIST: only explicitly permitted commands can run.
//   - Shell metacharacters that enable chaining/subshells are rejected.
//   - Pipes are allowed but each segment is validated against the allowlist.
//   - A blocklist is available as defense-in-depth (e.g., for future container execution).
//   - Output size is capped to prevent memory exhaustion.
//   - Optional manual approval: every command can require human confirmation before running.
class HostSkillExecutor : public SkillExecutor {
public:
    // Set the allowlist of permitted base commands (e.g., {"ls", "cat", "grep"}).
    // When non-empty, only these commands (and pipes between them) are allowed.
    void set_allowed_commands(const std::vector<std::string>& allowed);

    // Set blocked command patterns (defense-in-depth, substring match).
    void set_blocked_commands(const std::vector<std::string>& blocked);

    // Set maximum output size in bytes (0 = unlimited). Default: 1MB.
    void set_max_output_bytes(size_t max_bytes);

    // Set an approval callback. When set, every command must be approved before execution.
    // The callback receives the raw command string and returns true to allow, false to deny.
    void set_approval_callback(ApprovalCallback callback);

    Result execute(const SkillDef& skill, const nlohmann::json& arguments) override;

private:
    std::set<std::string> allowed_commands_;
    std::vector<std::string> blocked_commands_;
    size_t max_output_bytes_ = 1024 * 1024;  // 1MB default
    ApprovalCallback approval_callback_;

    // Escape a string for safe use inside single quotes in a shell command.
    static std::string shell_escape(const std::string& input);

    // Validate a command string against security policies.
    // Returns empty string if OK, or an error description if rejected.
    std::string validate_command(const std::string& command) const;

    // Check if a command matches any blocked pattern (substring match).
    bool is_blocked(const std::string& command) const;

    // Check for dangerous shell metacharacters that enable chaining or subshells.
    static std::string detect_shell_injection(const std::string& command);

    // Extract the base command name from a command string (first token, basename only).
    static std::string extract_base_command(const std::string& command);

    // Split a command string by unquoted pipe characters.
    static std::vector<std::string> split_pipe_segments(const std::string& command);
};
