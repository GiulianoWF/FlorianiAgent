#pragma once

#include <string>
#include "llm-link/llm_client.h"
#include "agent/agent.h"
#include "agent/pipeline.h"
#include "storage/history_store.h"
#include "agent/monitor.h"

class HttpServer {
public:
    HttpServer(int port, LlmClient& llm_client, Agent& agent,
               Pipeline& pipeline, HistoryStore& history,
               Monitor& monitor, const std::string& conversation_id);

    // Start the server (blocking).
    void start();

    // Stop the server.
    void stop();

private:
    int port_;
    LlmClient& llm_client_;
    Agent& agent_;
    Pipeline& pipeline_;
    HistoryStore& history_;
    Monitor& monitor_;
    std::string conversation_id_;
};
