#pragma once

#include <cstdint>
#include <string>

namespace chatroom {

struct RedisConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6379;
    std::string password;
    std::string channel = "chatroom.messages";

    static RedisConfig from_environment();
};

} // namespace chatroom
