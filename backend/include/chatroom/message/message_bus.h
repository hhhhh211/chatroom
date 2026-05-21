#pragma once

#include <functional>
#include <memory>
#include <string>

#include "chatroom/config/redis_config.h"

namespace chatroom {

struct BusMessage {
    std::string room_id;
    std::string content;
};

class MessageBus {
public:
    using MessageHandler = std::function<void(const BusMessage&)>;

    explicit MessageBus(RedisConfig config);
    ~MessageBus();

    MessageBus(const MessageBus&) = delete;
    MessageBus& operator=(const MessageBus&) = delete;

    bool available() const;
    void start(MessageHandler handler);
    void publish(const BusMessage& message);
    void stop();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace chatroom
