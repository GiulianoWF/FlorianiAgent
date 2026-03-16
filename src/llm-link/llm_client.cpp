#include "llm-link/llm_client.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <map>

using json = nlohmann::json;

// State for raw byte-forwarding (Phase 1 proxy mode)
struct ForwardState {
    std::function<void(const char*, size_t)> on_data;
    std::atomic<bool>* cancel;
};

// State for tool-call-aware SSE parsing
struct ChatState {
    std::string content;                          // accumulated text
    std::map<int, ToolCall> tool_calls_by_index;  // accumulated per-index
    std::string line_buffer;                      // partial SSE line
    std::string finish_reason;
    std::function<void(const std::string&)> on_token;
    std::atomic<bool>* cancel;
};

LlmClient::LlmClient(const std::string& backend_url) : backend_url_(backend_url) {}

// --- Raw forwarding callbacks ---

size_t LlmClient::forward_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto* state = static_cast<ForwardState*>(userdata);

    if (state->cancel && state->cancel->load()) {
        return 0;
    }

    if (state->on_data) {
        state->on_data(ptr, total);
    }

    return total;
}

bool LlmClient::forward(const std::string& request_body,
                         std::function<void(const char*, size_t)> on_data,
                         std::atomic<bool>* cancel) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[LlmClient] curl_easy_init failed\n";
        return false;
    }

    std::string url = backend_url_ + "/v1/chat/completions";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    ForwardState state;
    state.on_data = std::move(on_data);
    state.cancel = cancel;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, forward_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
        std::cerr << "[LlmClient] curl error: " << curl_easy_strerror(res) << "\n";
        return false;
    }

    return true;
}

// --- Tool-call-aware SSE parsing ---

static void process_sse_line(const std::string& line, ChatState& state) {
    if (line.rfind("data: ", 0) != 0) return;

    std::string data = line.substr(6);
    if (data == "[DONE]") return;

    try {
        auto j = json::parse(data);
        if (!j.contains("choices") || j["choices"].empty()) return;

        auto& choice = j["choices"][0];
        auto& delta = choice["delta"];

        // Accumulate text content
        if (delta.contains("content") && delta["content"].is_string()) {
            std::string token = delta["content"].get<std::string>();
            state.content += token;
            if (state.on_token) {
                state.on_token(token);
            }
        }

        // Accumulate tool calls (arguments arrive incrementally)
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (auto& tc_delta : delta["tool_calls"]) {
                int idx = tc_delta.value("index", 0);
                auto& tc = state.tool_calls_by_index[idx];

                if (tc_delta.contains("id") && tc_delta["id"].is_string()) {
                    tc.id = tc_delta["id"].get<std::string>();
                }
                if (tc_delta.contains("function")) {
                    auto& fn = tc_delta["function"];
                    if (fn.contains("name") && fn["name"].is_string()) {
                        tc.name = fn["name"].get<std::string>();
                    }
                    if (fn.contains("arguments") && fn["arguments"].is_string()) {
                        tc.arguments += fn["arguments"].get<std::string>();
                    }
                }
            }
        }

        // Capture finish reason
        if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
            state.finish_reason = choice["finish_reason"].get<std::string>();
        }
    } catch (const json::parse_error&) {
        // Skip malformed chunks
    }
}

size_t LlmClient::chat_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto* state = static_cast<ChatState*>(userdata);

    if (state->cancel && state->cancel->load()) {
        return 0;
    }

    state->line_buffer.append(ptr, total);

    std::istringstream stream(state->line_buffer);
    std::string line;
    std::string remaining;
    bool has_remaining = false;

    while (std::getline(stream, line)) {
        if (stream.eof() && state->line_buffer.back() != '\n') {
            remaining = line;
            has_remaining = true;
            break;
        }

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        process_sse_line(line, *state);
    }

    state->line_buffer = has_remaining ? remaining : "";
    return total;
}

LlmClient::CompletionResult LlmClient::chat(
        const std::vector<Message>& messages,
        const std::vector<SkillDef>& tools,
        std::function<void(const std::string&)> on_token,
        std::atomic<bool>* cancel) {

    // Build request payload
    json msg_array = json::array();
    for (const auto& m : messages) {
        msg_array.push_back(m.to_json());
    }

    json payload = {
        {"messages", msg_array},
        {"stream", true}
    };

    // Add tools if any
    if (!tools.empty()) {
        json tools_array = json::array();
        for (const auto& skill : tools) {
            tools_array.push_back(skill.to_tool_json());
        }
        payload["tools"] = tools_array;
    }

    std::string body = payload.dump();

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[LlmClient] curl_easy_init failed\n";
        return {};
    }

    std::string url = backend_url_ + "/v1/chat/completions";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    ChatState state;
    state.on_token = std::move(on_token);
    state.cancel = cancel;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chat_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    CompletionResult result;

    if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
        std::cerr << "[LlmClient] curl error: " << curl_easy_strerror(res) << "\n";
        return result;
    }

    result.success = true;
    result.content = std::move(state.content);

    // Collect tool calls from the index map
    for (auto& [idx, tc] : state.tool_calls_by_index) {
        result.tool_calls.push_back(std::move(tc));
    }

    return result;
}
