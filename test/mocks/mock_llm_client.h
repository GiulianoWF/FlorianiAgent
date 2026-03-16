#pragma once

#include <gmock/gmock.h>
#include "llm-link/i_llm_client.h"

class MockLlmClient : public ILlmClient {
public:
    MOCK_METHOD(CompletionResult, chat,
        (const std::vector<Message>&, const std::vector<SkillDef>&,
         std::function<void(const std::string&)>, std::atomic<bool>*),
        (override));
};
