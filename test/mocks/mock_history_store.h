#pragma once

#include <gmock/gmock.h>
#include "storage/i_history_store.h"

class MockHistoryStore : public IHistoryStore {
public:
    MOCK_METHOD(void, add_message,
        (const std::string&, const Message&), (override));
    MOCK_METHOD(std::vector<Message>, get_recent,
        (const std::string&, int), (override));
};
