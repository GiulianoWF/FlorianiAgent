#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "llm-link/i_llm_client.h"

class LlmClient : public ILlmClient {
public:
    explicit LlmClient(const std::string& backend_url);

    // Raw byte-forwarding mode (Phase 1 proxy pass-through).
    // Not part of ILlmClient — only used by HttpServer.
    bool forward(const std::string& request_body,
                 std::function<void(const char*, size_t)> on_data,
                 std::atomic<bool>* cancel = nullptr);

    // Tool-call-aware chat completion.
    CompletionResult chat(const std::vector<Message>& messages,
                          const std::vector<SkillDef>& tools = {},
                          std::function<void(const std::string&)> on_token = nullptr,
                          std::atomic<bool>* cancel = nullptr) override;

private:
    std::string backend_url_;

    static size_t forward_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t chat_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};
