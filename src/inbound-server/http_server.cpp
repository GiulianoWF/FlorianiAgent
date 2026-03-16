#include "inbound-server/http_server.h"
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

HttpServer::HttpServer(int port, LlmClient& llm_client, Agent& agent,
                       Pipeline& pipeline, HistoryStore& history,
                       Monitor& monitor, const std::string& conversation_id)
    : port_(port), llm_client_(llm_client), agent_(agent),
      pipeline_(pipeline), history_(history), monitor_(monitor),
      conversation_id_(conversation_id) {}

void HttpServer::start() {
    httplib::Server svr;

    // Health check
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // OpenAI-compatible chat completions endpoint
    svr.Post("/v1/chat/completions", [this](const httplib::Request& req, httplib::Response& res) {
        // Validate request body is valid JSON
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::parse_error& e) {
            res.status = 400;
            json err = {{"error", {{"message", std::string("Invalid JSON: ") + e.what()}, {"type", "invalid_request_error"}}}};
            res.set_content(err.dump(), "application/json");
            return;
        }

        // Extract user message from the request
        std::string user_message;
        if (body.contains("messages") && body["messages"].is_array()) {
            auto& msgs = body["messages"];
            for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
                if ((*it).value("role", "") == "user") {
                    user_message = (*it).value("content", "");
                    break;
                }
            }
        }

        if (user_message.empty()) {
            res.status = 400;
            json err = {{"error", {{"message", "No user message found"}, {"type", "invalid_request_error"}}}};
            res.set_content(err.dump(), "application/json");
            return;
        }

        bool streaming = body.value("stream", false);

        if (!streaming) {
            // Non-streaming: run pipeline, return full response
            PipelineContext ctx;
            ctx.user_message = user_message;
            ctx.original_request = body;
            ctx.streaming = false;
            ctx.conversation_id = conversation_id_;
            ctx.agent = &agent_;
            ctx.history = &history_;

            bool ok = pipeline_.run(ctx);

            if (!ok) {
                res.status = 500;
                json err = {{"error", {{"message", ctx.response_text.empty() ? "Pipeline failed" : ctx.response_text}, {"type", "server_error"}}}};
                res.set_content(err.dump(), "application/json");
                return;
            }

            json response = {
                {"id", "chatcmpl-agent"},
                {"object", "chat.completion"},
                {"choices", json::array({
                    {{"index", 0},
                     {"message", {{"role", "assistant"}, {"content", ctx.response_text}}},
                     {"finish_reason", "stop"}}
                })}
            };
            res.set_content(response.dump(), "application/json");
            return;
        }

        // Streaming: run pipeline with SSE token forwarding
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");

        auto* pipeline = &pipeline_;
        auto* agent = &agent_;
        auto* history = &history_;
        std::string msg = user_message;
        std::string conv_id = conversation_id_;
        json orig_request = body;

        res.set_chunked_content_provider(
            "text/event-stream",
            [pipeline, agent, history, msg, conv_id, orig_request](
                    size_t /*offset*/, httplib::DataSink& sink) -> bool {
                PipelineContext ctx;
                ctx.user_message = msg;
                ctx.original_request = orig_request;
                ctx.streaming = true;
                ctx.conversation_id = conv_id;
                ctx.agent = agent;
                ctx.history = history;
                ctx.on_token = [&sink](const std::string& token) {
                    json chunk = {
                        {"id", "chatcmpl-agent"},
                        {"object", "chat.completion.chunk"},
                        {"choices", json::array({
                            {{"index", 0},
                             {"delta", {{"content", token}}},
                             {"finish_reason", nullptr}}
                        })}
                    };
                    std::string event = "data: " + chunk.dump() + "\n\n";
                    sink.write(event.c_str(), event.size());
                };

                pipeline->run(ctx);

                std::string done = "data: [DONE]\n\n";
                sink.write(done.c_str(), done.size());
                sink.done();
                return true;
            }
        );
    });

    // Monitoring endpoint (Phase 6)
    svr.Get("/v1/monitor", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(monitor_.to_json().dump(2), "application/json");
    });

    std::cout << "[floriani-agent] Listening on port " << port_ << "\n";
    std::cout << "[floriani-agent] Ready (pipeline mode)\n";

    if (!svr.listen("0.0.0.0", port_)) {
        std::cerr << "[floriani-agent] Failed to start server on port " << port_ << "\n";
    }
}

void HttpServer::stop() {
    // For now, the server runs until the process is killed
}
