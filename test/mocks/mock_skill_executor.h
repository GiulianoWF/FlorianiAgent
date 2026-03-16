#pragma once

#include <gmock/gmock.h>
#include "skills/skill_executor.h"

class MockSkillExecutor : public SkillExecutor {
public:
    MOCK_METHOD(Result, execute,
        (const SkillDef&, const nlohmann::json&), (override));
};
