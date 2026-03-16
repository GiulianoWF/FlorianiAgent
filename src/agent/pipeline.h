#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <nlohmann/json.hpp>
#include "core/config.h"
#include "core/types.h"

class Agent;
class HistoryStore;

// Context passed through all pipeline stages for a single request.
struct PipelineContext {
    // Input
    std::string user_message;
    nlohmann::json original_request;  // raw client request JSON
    bool streaming = false;

    // State
    std::vector<Message> messages;     // accumulated messages for this request
    std::string conversation_id;

    // Output
    std::string response_text;
    std::function<void(const std::string&)> on_token;  // SSE token callback
    std::atomic<bool>* cancel = nullptr;

    // References to shared components
    Agent* agent = nullptr;
    HistoryStore* history = nullptr;
};

// Result of a single pipeline stage
struct StageResult {
    bool success = true;
    std::string error_message;
};

// A pipeline stage executor
using StageExecutor = std::function<StageResult(PipelineContext&)>;

// Config-driven processing pipeline.
// Each request flows through an ordered list of stages.
class Pipeline {
public:
    explicit Pipeline(const std::vector<PipelineStageConfig>& stage_configs);

    // Register a built-in stage executor
    void register_builtin(const std::string& name, StageExecutor executor);

    // Run the pipeline for a request.
    // Returns true if all stages completed successfully.
    bool run(PipelineContext& ctx);

private:
    struct Stage {
        PipelineStageConfig config;
        StageExecutor executor;
    };

    std::vector<Stage> stages_;
    std::map<std::string, StageExecutor> builtins_;

    StageResult handle_failure(const Stage& stage, const StageResult& result,
                               PipelineContext& ctx);
};
