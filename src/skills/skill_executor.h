#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "core/types.h"

// Abstract skill execution interface.
// V1: HostSkillExecutor runs skills as local processes.
// Future: DockerSkillExecutor runs skills in containers.
class SkillExecutor {
public:
    virtual ~SkillExecutor() = default;

    struct Result {
        bool success = false;
        std::string output;
        int exit_code = -1;
    };

    virtual Result execute(const SkillDef& skill, const nlohmann::json& arguments) = 0;
};
