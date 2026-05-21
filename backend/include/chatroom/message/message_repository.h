#pragma once

#include <memory>
#include <string>
#include <vector>

#include "chatroom/config/database_config.h"

namespace chatroom {

class MessageRepository {
public:
    explicit MessageRepository(DatabaseConfig config);
    ~MessageRepository();

    MessageRepository(const MessageRepository&) = delete;
    MessageRepository& operator=(const MessageRepository&) = delete;

    bool available() const;
    void initialize_schema();
    bool save_message(const std::string& room_id, const std::string& raw_message);
    std::vector<std::string> load_recent_messages(const std::string& room_id);

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace chatroom
