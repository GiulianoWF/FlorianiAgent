#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "agent/agent.h"
#include "mocks/mock_llm_client.h"
#include "mocks/mock_skill_executor.h"
#include "mocks/mock_history_store.h"

TEST(AgentTest, SimpleTextResponse) {
    MockLlmClient llm;
    MockSkillExecutor skills;

    ILlmClient::CompletionResult text_result;
    text_result.success = true;
    text_result.content = "Hello, world!";

    EXPECT_CALL(llm, chat(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(text_result));

    Agent agent(llm, skills, "You are helpful.", {});
    std::string response = agent.run("Hi");

    EXPECT_EQ(response, "Hello, world!");
}

TEST(AgentTest, ToolCallThenTextResponse) {
    MockLlmClient llm;
    MockSkillExecutor skills;

    SkillDef shell_skill;
    shell_skill.name = "shell_exec";
    shell_skill.description = "Run a command";
    shell_skill.executor_command = "/bin/bash -c";

    // First LLM call returns a tool call
    ILlmClient::CompletionResult tool_result;
    tool_result.success = true;
    tool_result.tool_calls = {{"call_1", "shell_exec", R"({"command":"date"})"}};

    // Second LLM call returns text
    ILlmClient::CompletionResult final_result;
    final_result.success = true;
    final_result.content = "The date is today.";

    EXPECT_CALL(llm, chat(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(tool_result))
        .WillOnce(testing::Return(final_result));

    EXPECT_CALL(skills, execute(testing::_, testing::_))
        .WillOnce(testing::Return(SkillExecutor::Result{true, "Mon Mar 15", 0}));

    Agent agent(llm, skills, "You are helpful.", {shell_skill});
    std::string response = agent.run("What day is it?");

    EXPECT_EQ(response, "The date is today.");
}

TEST(AgentTest, MaxIterationsReached) {
    MockLlmClient llm;
    MockSkillExecutor skills;

    SkillDef tool;
    tool.name = "some_tool";
    tool.executor_command = "echo";

    ILlmClient::CompletionResult tool_result;
    tool_result.success = true;
    tool_result.tool_calls = {{"call_1", "some_tool", R"({})"}};

    // Always return tool calls, never text
    EXPECT_CALL(llm, chat(testing::_, testing::_, testing::_, testing::_))
        .WillRepeatedly(testing::Return(tool_result));
    EXPECT_CALL(skills, execute(testing::_, testing::_))
        .WillRepeatedly(testing::Return(SkillExecutor::Result{true, "ok", 0}));

    Agent agent(llm, skills, "", {tool}, /*max_iterations=*/3);
    std::string response = agent.run("loop forever");

    EXPECT_EQ(response, "[max tool iterations reached]");
}

TEST(AgentTest, LlmFailure) {
    MockLlmClient llm;
    MockSkillExecutor skills;

    ILlmClient::CompletionResult fail_result;
    fail_result.success = false;

    EXPECT_CALL(llm, chat(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(fail_result));

    Agent agent(llm, skills, "", {});
    std::string response = agent.run("test");

    EXPECT_EQ(response, "[LLM request failed]");
}

TEST(AgentTest, HistoryPersistence) {
    MockLlmClient llm;
    MockSkillExecutor skills;
    MockHistoryStore history;

    EXPECT_CALL(history, get_recent("conv1", 20))
        .WillOnce(testing::Return(std::vector<Message>{}));
    // user message + assistant response = 2 add_message calls
    EXPECT_CALL(history, add_message("conv1", testing::_))
        .Times(2);

    ILlmClient::CompletionResult text_result;
    text_result.success = true;
    text_result.content = "response";

    EXPECT_CALL(llm, chat(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(text_result));

    Agent agent(llm, skills, "", {});
    agent.set_history_store(&history, "conv1");
    std::string response = agent.run("hello");

    EXPECT_EQ(response, "response");
}

TEST(AgentTest, UnknownToolReturnsError) {
    MockLlmClient llm;
    MockSkillExecutor skills;

    // LLM requests a tool that doesn't exist in available_skills
    ILlmClient::CompletionResult tool_result;
    tool_result.success = true;
    tool_result.tool_calls = {{"call_1", "nonexistent_tool", R"({})"}};

    ILlmClient::CompletionResult final_result;
    final_result.success = true;
    final_result.content = "Got the error.";

    EXPECT_CALL(llm, chat(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(tool_result))
        .WillOnce(testing::Return(final_result));

    Agent agent(llm, skills, "", {});
    std::string response = agent.run("use a tool");

    EXPECT_EQ(response, "Got the error.");
}

TEST(AgentTest, Cancellation) {
    MockLlmClient llm;
    MockSkillExecutor skills;

    std::atomic<bool> cancel{true};

    Agent agent(llm, skills, "", {});
    std::string response = agent.run("test", nullptr, &cancel);

    EXPECT_EQ(response, "[cancelled]");
}
