#pragma once

#include "skills/skill_executor.h"

// Executes skills as local host processes via popen().
class HostSkillExecutor : public SkillExecutor {
public:
    Result execute(const SkillDef& skill, const nlohmann::json& arguments) override;
};
