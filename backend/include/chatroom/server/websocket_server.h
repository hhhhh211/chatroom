#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

namespace chatroom {

class MessageBus;
class MessageRepository;
class WebSocketSession;

struct ServerStats {
    std::size_t online_count = 0;
    std::size_t room_count = 0;
};

class WebSocketServer {
public:
    WebSocketServer(
        std::uint16_t port,
        std::shared_ptr<MessageRepository> message_repository,
        std::shared_ptr<MessageBus> message_bus);

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    void run();
    void handle_client_message(const std::string& room_id, const std::string& message);
    void broadcast_local(const std::string& room_id, const std::string& message);
    void broadcast_stats(const std::string& room_id);
    void broadcast_all_stats();
    ServerStats stats_for_room(const std::string& room_id);
    std::vector<std::string> load_recent_messages(const std::string& room_id);
    void remove_session(const std::shared_ptr<WebSocketSession>& session);

private:
    std::uint16_t port_;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<MessageRepository> message_repository_;
    std::shared_ptr<MessageBus> message_bus_;
    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::vector<std::weak_ptr<WebSocketSession>>> rooms_;
};

} // namespace chatroom
