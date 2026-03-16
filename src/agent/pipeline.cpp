#include "agent/pipeline.h"
#include <iostream>

Pipeline::Pipeline(const std::vector<PipelineStageConfig>& stage_configs) {
    for (const auto& cfg : stage_configs) {
        stages_.push_back({cfg, nullptr});
    }
}

void Pipeline::register_builtin(const std::string& name, StageExecutor executor) {
    builtins_[name] = std::move(executor);
}

bool Pipeline::run(PipelineContext& ctx) {
    // Resolve executors for each stage
    for (auto& stage : stages_) {
        if (stage.executor) continue;

        const auto& exec_name = stage.config.executor;
        if (exec_name.rfind("builtin:", 0) == 0) {
            std::string builtin_name = exec_name.substr(8);
            auto it = builtins_.find(builtin_name);
            if (it != builtins_.end()) {
                stage.executor = it->second;
            } else {
                std::cerr << "[Pipeline] Unknown builtin: " << builtin_name << "\n";
                return false;
            }
        } else {
            std::cerr << "[Pipeline] Unknown executor type: " << exec_name << "\n";
            return false;
        }
    }

    // Execute stages in order
    for (auto& stage : stages_) {
        std::cout << "[Pipeline] Running stage: " << stage.config.name << "\n";

        auto result = stage.executor(ctx);

        if (!result.success) {
            std::cerr << "[Pipeline] Stage '" << stage.config.name
                      << "' failed: " << result.error_message << "\n";

            auto recovery = handle_failure(stage, result, ctx);
            if (!recovery.success) {
                return false;
            }
        }
    }

    return true;
}

StageResult Pipeline::handle_failure(const Stage& stage, const StageResult& result,
                                      PipelineContext& ctx) {
    const auto& policy = stage.config.failure_policy;

    if (policy == "skip") {
        std::cout << "[Pipeline] Skipping failed stage: " << stage.config.name << "\n";
        return {true, ""};
    }

    if (policy == "abort") {
        ctx.response_text = "Error in stage '" + stage.config.name + "': " + result.error_message;
        return {false, result.error_message};
    }

    if (policy == "retry_with_feedback") {
        // Feed the error back as context and re-run
        std::cout << "[Pipeline] Retrying stage with feedback: " << stage.config.name << "\n";
        ctx.messages.push_back({"system",
            "The previous attempt failed with error: " + result.error_message +
            ". Please try again with a different approach.", "", {}});

        auto retry_result = stage.executor(ctx);
        if (!retry_result.success) {
            ctx.response_text = "Error in stage '" + stage.config.name +
                                "' (retry failed): " + retry_result.error_message;
            return {false, retry_result.error_message};
        }
        return {true, ""};
    }

    if (policy == "fallback") {
        const auto& fallback_name = stage.config.fallback_stage;
        auto it = builtins_.find(fallback_name);
        if (it != builtins_.end()) {
            std::cout << "[Pipeline] Falling back to: " << fallback_name << "\n";
            return it->second(ctx);
        }
        std::cerr << "[Pipeline] Fallback stage not found: " << fallback_name << "\n";
        return {false, "Fallback stage '" + fallback_name + "' not found"};
    }

    // Unknown policy — treat as abort
    return {false, result.error_message};
}
