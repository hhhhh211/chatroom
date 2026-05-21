#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "chatroom/config/database_config.h"
#include "chatroom/config/redis_config.h"
#include "chatroom/message/message_bus.h"
#include "chatroom/message/message_repository.h"
#include "chatroom/server/websocket_server.h"

int main(int argc, char* argv[]) {
    std::uint16_t port = 8080;

    if (argc > 1) {
        const int parsed_port = std::atoi(argv[1]);
        if (parsed_port <= 0 || parsed_port > 65535) {
            std::cerr << "invalid port: " << argv[1] << '\n';
            return 1;
        }
        port = static_cast<std::uint16_t>(parsed_port);
    }

    try {
        auto message_repository =
            std::make_shared<chatroom::MessageRepository>(chatroom::DatabaseConfig::from_environment());
        message_repository->initialize_schema();

        auto message_bus = std::make_shared<chatroom::MessageBus>(chatroom::RedisConfig::from_environment());

        chatroom::WebSocketServer server(port, message_repository, message_bus);
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "chatroom backend failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
