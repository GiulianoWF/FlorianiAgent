#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "core/types.h"

// Abstract LLM client interface.
// V1: LlmClient connects to llama.cpp via libcurl.
// Tests: MockLlmClient injects canned responses.
class ILlmClient {
public:
    virtual ~ILlmClient() = default;

    struct CompletionResult {
        std::string content;
        std::vector<ToolCall> tool_calls;
        bool success = false;

        bool has_tool_calls() const { return !tool_calls.empty(); }
    };

    // Tool-call-aware chat completion.
    virtual CompletionResult chat(const std::vector<Message>& messages,
                                  const std::vector<SkillDef>& tools = {},
                                  std::function<void(const std::string&)> on_token = nullptr,
                                  std::atomic<bool>* cancel = nullptr) = 0;
};
