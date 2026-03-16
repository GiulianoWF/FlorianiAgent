#include "storage/history_store.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

static std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static std::string generate_id() {
    // Simple ID based on timestamp + random suffix
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "conv_" + std::to_string(ms);
}

HistoryStore::HistoryStore(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("[HistoryStore] Failed to open DB: " + err);
    }

    // Enable WAL mode for better concurrent access
    exec("PRAGMA journal_mode=WAL");
    init_schema();

    std::cout << "[HistoryStore] Opened " << db_path << "\n";
}

HistoryStore::~HistoryStore() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void HistoryStore::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("[HistoryStore] SQL error: " + msg);
    }
}

void HistoryStore::init_schema() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS conversations (
            id TEXT PRIMARY KEY,
            created_at TEXT NOT NULL
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT,
            tool_call_id TEXT,
            tool_calls_json TEXT,
            timestamp TEXT NOT NULL,
            FOREIGN KEY (conversation_id) REFERENCES conversations(id)
        )
    )");

    exec(R"(
        CREATE INDEX IF NOT EXISTS idx_messages_conv
        ON messages(conversation_id, id)
    )");
}

std::string HistoryStore::create_conversation() {
    std::string id = generate_id();
    std::string ts = now_iso8601();

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO conversations (id, created_at) VALUES (?, ?)", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return id;
}

std::string HistoryStore::get_or_create_conversation() {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id FROM conversations ORDER BY created_at DESC LIMIT 1", -1, &stmt, nullptr);

    std::string id;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);

    if (id.empty()) {
        id = create_conversation();
    }

    return id;
}

void HistoryStore::add_message(const std::string& conversation_id, const Message& msg) {
    std::string ts = now_iso8601();

    // Serialize tool_calls to JSON if present
    std::string tool_calls_json;
    if (!msg.tool_calls.empty()) {
        json tc_array = json::array();
        for (const auto& tc : msg.tool_calls) {
            tc_array.push_back({
                {"id", tc.id},
                {"name", tc.name},
                {"arguments", tc.arguments}
            });
        }
        tool_calls_json = tc_array.dump();
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO messages (conversation_id, role, content, tool_call_id, tool_calls_json, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, msg.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, msg.content.c_str(), -1, SQLITE_TRANSIENT);

    if (msg.tool_call_id.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        sqlite3_bind_text(stmt, 4, msg.tool_call_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    if (tool_calls_json.empty()) {
        sqlite3_bind_null(stmt, 5);
    } else {
        sqlite3_bind_text(stmt, 5, tool_calls_json.c_str(), -1, SQLITE_TRANSIENT);
    }

    sqlite3_bind_text(stmt, 6, ts.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "[HistoryStore] Failed to insert message: " << sqlite3_errmsg(db_) << "\n";
    }
    sqlite3_finalize(stmt);
}

std::vector<Message> HistoryStore::get_recent(const std::string& conversation_id, int limit) {
    // Get messages ordered by id ascending, but only the last N
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT role, content, tool_call_id, tool_calls_json FROM ("
        "  SELECT * FROM messages WHERE conversation_id = ? ORDER BY id DESC LIMIT ?"
        ") sub ORDER BY id ASC",
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, conversation_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    std::vector<Message> messages;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message msg;
        msg.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            msg.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }

        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            msg.tool_call_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        }

        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            std::string tc_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            try {
                auto tc_array = json::parse(tc_json);
                for (const auto& tc : tc_array) {
                    msg.tool_calls.push_back({
                        tc.value("id", ""),
                        tc.value("name", ""),
                        tc.value("arguments", "")
                    });
                }
            } catch (const json::parse_error&) {
                // skip malformed tool_calls
            }
        }

        messages.push_back(std::move(msg));
    }
    sqlite3_finalize(stmt);

    return messages;
}

std::vector<std::string> HistoryStore::list_conversations(int limit) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id FROM conversations ORDER BY created_at DESC LIMIT ?",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    std::vector<std::string> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    return ids;
}
