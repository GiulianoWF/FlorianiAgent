#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include "storage/i_history_store.h"

// Persists conversation history in SQLite.
// Schema: conversations(id, created_at), messages(id, conversation_id, role, content,
// tool_call_id, tool_calls_json, timestamp).
class HistoryStore : public IHistoryStore {
public:
    explicit HistoryStore(const std::string& db_path);
    ~HistoryStore();

    // Non-copyable
    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    // Create a new conversation and return its ID
    std::string create_conversation();

    // Get the most recent conversation ID (or create one if none exist)
    std::string get_or_create_conversation();

    // Add a message to a conversation
    void add_message(const std::string& conversation_id, const Message& msg) override;

    // Get the most recent N messages for a conversation
    std::vector<Message> get_recent(const std::string& conversation_id, int limit = 20) override;

    // List all conversation IDs (most recent first)
    std::vector<std::string> list_conversations(int limit = 50);

private:
    sqlite3* db_ = nullptr;

    void init_schema();
    void exec(const std::string& sql);
};
