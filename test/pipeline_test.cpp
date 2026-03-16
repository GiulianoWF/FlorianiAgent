#include <gtest/gtest.h>
#include "agent/pipeline.h"

TEST(PipelineTest, RunsAllStagesInOrder) {
    Pipeline pipeline({
        {"stage_a", "builtin:a", "abort", ""},
        {"stage_b", "builtin:b", "abort", ""},
        {"stage_c", "builtin:c", "abort", ""},
    });

    std::string execution_order;

    pipeline.register_builtin("a", [&](PipelineContext&) -> StageResult {
        execution_order += "a";
        return {true, ""};
    });
    pipeline.register_builtin("b", [&](PipelineContext&) -> StageResult {
        execution_order += "b";
        return {true, ""};
    });
    pipeline.register_builtin("c", [&](PipelineContext&) -> StageResult {
        execution_order += "c";
        return {true, ""};
    });

    PipelineContext ctx;
    EXPECT_TRUE(pipeline.run(ctx));
    EXPECT_EQ(execution_order, "abc");
}

TEST(PipelineTest, StageCanModifyContext) {
    Pipeline pipeline({
        {"set_response", "builtin:set_response", "abort", ""},
    });

    pipeline.register_builtin("set_response", [](PipelineContext& ctx) -> StageResult {
        ctx.response_text = "hello from pipeline";
        return {true, ""};
    });

    PipelineContext ctx;
    EXPECT_TRUE(pipeline.run(ctx));
    EXPECT_EQ(ctx.response_text, "hello from pipeline");
}

TEST(PipelineTest, AbortPolicyStopsPipeline) {
    Pipeline pipeline({
        {"failing", "builtin:failing", "abort", ""},
        {"should_not_run", "builtin:noop", "abort", ""},
    });

    bool second_ran = false;

    pipeline.register_builtin("failing", [](PipelineContext&) -> StageResult {
        return {false, "something broke"};
    });
    pipeline.register_builtin("noop", [&](PipelineContext&) -> StageResult {
        second_ran = true;
        return {true, ""};
    });

    PipelineContext ctx;
    EXPECT_FALSE(pipeline.run(ctx));
    EXPECT_FALSE(second_ran);
    EXPECT_EQ(ctx.response_text, "Error in stage 'failing': something broke");
}

TEST(PipelineTest, SkipPolicyContinuesPipeline) {
    Pipeline pipeline({
        {"skippable", "builtin:skippable", "skip", ""},
        {"final", "builtin:final", "abort", ""},
    });

    pipeline.register_builtin("skippable", [](PipelineContext&) -> StageResult {
        return {false, "non-critical failure"};
    });
    pipeline.register_builtin("final", [](PipelineContext& ctx) -> StageResult {
        ctx.response_text = "completed";
        return {true, ""};
    });

    PipelineContext ctx;
    EXPECT_TRUE(pipeline.run(ctx));
    EXPECT_EQ(ctx.response_text, "completed");
}

TEST(PipelineTest, RetryWithFeedbackSucceedsOnRetry) {
    Pipeline pipeline({
        {"flaky", "builtin:flaky", "retry_with_feedback", ""},
    });

    int call_count = 0;
    pipeline.register_builtin("flaky", [&](PipelineContext&) -> StageResult {
        call_count++;
        if (call_count == 1) {
            return {false, "transient error"};
        }
        return {true, ""};
    });

    PipelineContext ctx;
    EXPECT_TRUE(pipeline.run(ctx));
    EXPECT_EQ(call_count, 2);
    // Verify feedback message was added to context
    ASSERT_FALSE(ctx.messages.empty());
    EXPECT_EQ(ctx.messages.back().role, "system");
}

TEST(PipelineTest, RetryWithFeedbackFailsBothTimes) {
    Pipeline pipeline({
        {"always_fails", "builtin:always_fails", "retry_with_feedback", ""},
    });

    pipeline.register_builtin("always_fails", [](PipelineContext&) -> StageResult {
        return {false, "persistent error"};
    });

    PipelineContext ctx;
    EXPECT_FALSE(pipeline.run(ctx));
    EXPECT_EQ(ctx.response_text,
              "Error in stage 'always_fails' (retry failed): persistent error");
}

TEST(PipelineTest, FallbackPolicyRunsFallbackStage) {
    Pipeline pipeline({
        {"primary", "builtin:primary", "fallback", "backup"},
    });

    pipeline.register_builtin("primary", [](PipelineContext&) -> StageResult {
        return {false, "primary failed"};
    });
    pipeline.register_builtin("backup", [](PipelineContext& ctx) -> StageResult {
        ctx.response_text = "handled by backup";
        return {true, ""};
    });

    PipelineContext ctx;
    EXPECT_TRUE(pipeline.run(ctx));
    EXPECT_EQ(ctx.response_text, "handled by backup");
}

TEST(PipelineTest, FallbackPolicyFailsWhenFallbackNotFound) {
    Pipeline pipeline({
        {"primary", "builtin:primary", "fallback", "nonexistent"},
    });

    pipeline.register_builtin("primary", [](PipelineContext&) -> StageResult {
        return {false, "primary failed"};
    });

    PipelineContext ctx;
    EXPECT_FALSE(pipeline.run(ctx));
}

TEST(PipelineTest, UnknownBuiltinFailsToRun) {
    Pipeline pipeline({
        {"bad_stage", "builtin:does_not_exist", "abort", ""},
    });

    PipelineContext ctx;
    EXPECT_FALSE(pipeline.run(ctx));
}

TEST(PipelineTest, UnknownExecutorTypeFailsToRun) {
    Pipeline pipeline({
        {"bad_stage", "external:something", "abort", ""},
    });

    PipelineContext ctx;
    EXPECT_FALSE(pipeline.run(ctx));
}

TEST(PipelineTest, ContextFlowsBetweenStages) {
    Pipeline pipeline({
        {"add_message", "builtin:add_message", "abort", ""},
        {"check_message", "builtin:check_message", "abort", ""},
    });

    pipeline.register_builtin("add_message", [](PipelineContext& ctx) -> StageResult {
        ctx.messages.push_back({"user", "hello", "", {}});
        return {true, ""};
    });

    bool message_found = false;
    pipeline.register_builtin("check_message", [&](PipelineContext& ctx) -> StageResult {
        message_found = (!ctx.messages.empty() && ctx.messages[0].content == "hello");
        return {true, ""};
    });

    PipelineContext ctx;
    EXPECT_TRUE(pipeline.run(ctx));
    EXPECT_TRUE(message_found);
}
