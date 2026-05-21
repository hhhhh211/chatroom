#pragma once

#include <cstdint>
#include <string>

namespace chatroom {

struct DatabaseConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string user = "chatroom";
    std::string password = "chatroom";
    std::string database = "chatroom";
    unsigned int history_limit = 50;

    static DatabaseConfig from_environment();
};

} // namespace chatroom
