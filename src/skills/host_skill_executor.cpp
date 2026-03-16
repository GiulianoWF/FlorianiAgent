#include "skills/host_skill_executor.h"
#include <cstdio>
#include <array>
#include <iostream>
#include <sstream>

SkillExecutor::Result HostSkillExecutor::execute(const SkillDef& skill,
                                                  const nlohmann::json& arguments) {
    Result result;

    // Build command: executor_command receives arguments as a JSON string via stdin
    // For simple executors like "/bin/bash -c", we pass the relevant argument directly
    std::string command;

    if (arguments.contains("command") && arguments["command"].is_string()) {
        // Special case: if the skill has a "command" parameter, pass it directly
        // to the executor (e.g., "/bin/bash -c 'the command'")
        std::string arg = arguments["command"].get<std::string>();
        command = skill.executor_command + " '" + arg + "'";
    } else {
        // General case: pipe the full arguments JSON to the executor via stdin
        std::string args_json = arguments.dump();
        command = "echo '" + args_json + "' | " + skill.executor_command;
    }

    std::cout << "[SkillExecutor] Running: " << command << "\n";

    // Execute via popen and capture output
    std::array<char, 4096> buffer;
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "Failed to execute command";
        result.exit_code = -1;
        return result;
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = pclose(pipe);
    result.exit_code = WEXITSTATUS(status);
    result.success = (result.exit_code == 0);
    result.output = output;

    // Trim trailing newline
    if (!result.output.empty() && result.output.back() == '\n') {
        result.output.pop_back();
    }

    std::cout << "[SkillExecutor] Exit code: " << result.exit_code
              << ", output: " << result.output.substr(0, 200) << "\n";

    return result;
}
