#pragma once

#include <string>
#include <vector>
#include "core/types.h"

// Abstract history store interface.
// V1: HistoryStore persists to SQLite.
// Tests: MockHistoryStore injects canned history.
class IHistoryStore {
public:
    virtual ~IHistoryStore() = default;

    virtual void add_message(const std::string& conversation_id, const Message& msg) = 0;
    virtual std::vector<Message> get_recent(const std::string& conversation_id, int limit = 20) = 0;
};
