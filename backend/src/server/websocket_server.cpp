#include "chatroom/server/websocket_server.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "chatroom/message/message_bus.h"
#include "chatroom/message/message_repository.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

namespace chatroom {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

constexpr std::size_t kMaxMessageBytes = 1024;
constexpr std::size_t kMaxWebSocketFrameBytes = 4096;
constexpr std::size_t kMaxSendQueueMessages = 256;
constexpr std::size_t kMaxSendQueueBytes = 256 * 1024;
constexpr std::size_t kMaxMessagesPerWindow = 8;
constexpr auto kRateLimitWindow = std::chrono::seconds(10);
constexpr auto kHandshakeTimeout = std::chrono::seconds(30);
constexpr auto kIdleTimeout = std::chrono::seconds(75);
constexpr std::string_view kControlPrefix = "__chatroom_control__";

constexpr std::string_view kHomePage = R"HTML(<!doctype html>
<html lang="zh-CN">
    <head>
        <meta charset="UTF-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1.0" />
        <title>C++ 高并发聊天室</title>
        <style>
            * {
                box-sizing: border-box;
            }

            body {
                margin: 0;
                min-height: 100vh;
                display: grid;
                place-items: center;
                padding: 24px;
                font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
                color: #f9fafb;
                background:
                    linear-gradient(135deg, rgba(246, 184, 63, 0.18) 0 1px, transparent 1px 18px),
                    #151718;
            }

            main {
                width: min(680px, 100%);
                border: 1px solid rgba(255, 255, 255, 0.16);
                border-radius: 8px;
                padding: 28px;
                background: #f3f1e9;
                color: #171a1d;
                box-shadow: 0 24px 60px rgba(0, 0, 0, 0.28);
            }

            p {
                line-height: 1.7;
            }

            code {
                display: block;
                margin-top: 12px;
                border-radius: 6px;
                padding: 12px;
                overflow-x: auto;
                background: #171a1d;
                color: #f6b83f;
            }
        </style>
    </head>
    <body>
        <main>
            <h1>C++ 高并发聊天室后端</h1>
            <p>后端服务已经启动。聊天页面由 frontend 目录里的 Vue 应用提供。</p>
            <p>请另开终端启动前端：</p>
            <code>cd frontend<br />npm install<br />npm run dev</code>
            <p>然后访问 Vite 输出的地址，通常是 http://127.0.0.1:5173/。</p>
            <p>第 4 阶段使用 MySQL 保存消息历史，数据库连接通过 CHATROOM_MYSQL_* 环境变量配置。</p>
            <p>第 5 阶段使用 Redis Pub/Sub 做多后端实例消息广播，Redis 连接通过 CHATROOM_REDIS_* 环境变量配置。</p>
            <p>第 6 阶段已经加入心跳、发送队列、消息长度和发送频率保护，以及在线人数统计。</p>
        </main>
    </body>
</html>
)HTML";

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string url_decode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (value[i] == '%' && i + 2U < value.size()) {
            const int high = hex_value(value[i + 1U]);
            const int low = hex_value(value[i + 2U]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                i += 2U;
                continue;
            }
        }

        decoded.push_back(value[i]);
    }

    return decoded;
}

std::string normalize_room_id(std::string room_id) {
    const auto first = room_id.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "lobby";
    }

    const auto last = room_id.find_last_not_of(" \t\r\n");
    room_id = room_id.substr(first, last - first + 1U);

    if (room_id.empty()) {
        return "lobby";
    }

    constexpr std::size_t max_room_id_length = 64;
    if (room_id.size() > max_room_id_length) {
        room_id.resize(max_room_id_length);
    }

    return room_id;
}

std::string extract_room_id(std::string_view target) {
    const std::size_t query_pos = target.find('?');
    if (query_pos == std::string_view::npos || query_pos + 1U >= target.size()) {
        return "lobby";
    }

    std::string_view query = target.substr(query_pos + 1U);
    while (!query.empty()) {
        const std::size_t amp_pos = query.find('&');
        const std::string_view pair = query.substr(0, amp_pos);
        const std::size_t equal_pos = pair.find('=');

        if (equal_pos != std::string_view::npos && pair.substr(0, equal_pos) == "room_id") {
            return normalize_room_id(url_decode(pair.substr(equal_pos + 1U)));
        }

        if (amp_pos == std::string_view::npos) {
            break;
        }
        query.remove_prefix(amp_pos + 1U);
    }

    return "lobby";
}

std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

bool is_control_message(std::string_view message) {
    return message.substr(0, kControlPrefix.size()) == kControlPrefix;
}

std::string make_control_notice(std::string_view text) {
    return std::string(kControlPrefix) + R"({"type":"notice","text":")" + json_escape(text) + R"("})";
}

std::string make_control_stats(const ServerStats& stats) {
    return std::string(kControlPrefix) + R"({"type":"stats","onlineCount":)" +
           std::to_string(stats.online_count) + R"(,"roomOnlineCount":)" + std::to_string(stats.room_count) + "}";
}

} // namespace

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    WebSocketSession(tcp::socket socket, WebSocketServer& server, std::string room_id)
        : websocket_(std::move(socket)), server_(server), room_id_(std::move(room_id)) {}

    const std::string& room_id() const {
        return room_id_;
    }

    void run(http::request<http::string_body> request) {
        try {
            websocket::stream_base::timeout timeout;
            timeout.handshake_timeout = kHandshakeTimeout;
            timeout.idle_timeout = kIdleTimeout;
            timeout.keep_alive_pings = true;

            websocket_.set_option(timeout);
            websocket_.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& response) {
                    response.set(http::field::server, "chatroom-beast");
                }));
            websocket_.read_message_max(kMaxWebSocketFrameBytes);
            websocket_.accept(request);
            start_writer();

            std::cout << "client connected to room: " << room_id_ << '\n';
            send_history();
            server_.broadcast_all_stats();
            read_loop();
        } catch (const beast::system_error& error) {
            if (error.code() != websocket::error::closed) {
                std::cerr << "websocket session error: " << error.code().message() << '\n';
            }
        }

        stop_writer();
        server_.remove_session(shared_from_this());
        std::cout << "client disconnected\n";
    }

    bool enqueue_text(std::string message) {
        if (message.size() > kMaxSendQueueBytes) {
            return false;
        }

        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stopping_) {
            return false;
        }

        if (outgoing_messages_.size() >= kMaxSendQueueMessages ||
            queued_bytes_ + message.size() > kMaxSendQueueBytes) {
            std::cerr << "dropping websocket message because send queue is full in room: " << room_id_ << '\n';
            return false;
        }

        queued_bytes_ += message.size();
        outgoing_messages_.push_back(std::move(message));
        queue_cv_.notify_one();
        return true;
    }

private:
    void start_writer() {
        writer_ = std::thread([this] {
            write_loop();
        });
    }

    void stop_writer() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stopping_ = true;
            outgoing_messages_.clear();
            queued_bytes_ = 0;
        }

        beast::error_code error;
        websocket_.next_layer().shutdown(tcp::socket::shutdown_both, error);
        websocket_.next_layer().close(error);

        queue_cv_.notify_all();
        if (writer_.joinable()) {
            writer_.join();
        }
    }

    void write_loop() {
        while (true) {
            std::string message;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] {
                    return stopping_ || !outgoing_messages_.empty();
                });

                if (stopping_) {
                    break;
                }

                message = std::move(outgoing_messages_.front());
                outgoing_messages_.pop_front();
                queued_bytes_ -= message.size();
            }

            beast::error_code error;
            websocket_.text(true);
            websocket_.write(asio::buffer(message), error);
            if (error) {
                std::cerr << "websocket send error: " << error.message() << '\n';
                std::lock_guard<std::mutex> lock(queue_mutex_);
                stopping_ = true;
                outgoing_messages_.clear();
                queued_bytes_ = 0;
                break;
            }
        }
    }

    void send_history() {
        const auto messages = server_.load_recent_messages(room_id_);
        for (const auto& message : messages) {
            if (!enqueue_text(message)) {
                break;
            }
        }
    }

    bool allow_client_message() {
        const auto now = std::chrono::steady_clock::now();
        while (!recent_messages_.empty() && now - recent_messages_.front() > kRateLimitWindow) {
            recent_messages_.pop_front();
        }

        if (recent_messages_.size() >= kMaxMessagesPerWindow) {
            return false;
        }

        recent_messages_.push_back(now);
        return true;
    }

    void read_loop() {
        while (true) {
            beast::flat_buffer buffer;
            beast::error_code error;
            websocket_.read(buffer, error);

            if (error == websocket::error::closed) {
                break;
            }

            if (error) {
                if (error == beast::error::timeout) {
                    std::cerr << "websocket heartbeat timeout in room: " << room_id_ << '\n';
                }
                std::cerr << "websocket read error: " << error.message() << '\n';
                break;
            }

            if (!websocket_.got_text()) {
                continue;
            }

            std::string message = beast::buffers_to_string(buffer.data());
            if (is_control_message(message)) {
                continue;
            }

            if (message.size() > kMaxMessageBytes) {
                enqueue_text(make_control_notice("消息太长，最多 1024 字节。"));
                continue;
            }

            if (!allow_client_message()) {
                enqueue_text(make_control_notice("发送太快了，请稍后再试。"));
                continue;
            }

            server_.handle_client_message(room_id_, message);
        }
    }

    websocket::stream<tcp::socket> websocket_;
    WebSocketServer& server_;
    std::string room_id_;
    std::thread writer_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::string> outgoing_messages_;
    std::deque<std::chrono::steady_clock::time_point> recent_messages_;
    std::size_t queued_bytes_ = 0;
    bool stopping_ = false;
};

WebSocketServer::WebSocketServer(
    std::uint16_t port,
    std::shared_ptr<MessageRepository> message_repository,
    std::shared_ptr<MessageBus> message_bus)
    : port_(port),
      io_context_(),
      acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)),
      message_repository_(std::move(message_repository)),
      message_bus_(std::move(message_bus)) {
    if (message_bus_) {
        message_bus_->start([this](const BusMessage& message) {
            broadcast_local(message.room_id, message.content);
        });
    }
}

void WebSocketServer::run() {
    std::cout << "chatroom server listening on http://127.0.0.1:" << port_ << '\n';
    std::cout << "websocket endpoint ws://127.0.0.1:" << port_ << '\n';

    while (true) {
        tcp::socket socket(io_context_);
        acceptor_.accept(socket);

        std::thread([this, socket = std::move(socket)]() mutable {
            beast::error_code error;
            beast::flat_buffer buffer;
            http::request<http::string_body> request;
            http::read(socket, buffer, request, error);

            if (error) {
                std::cerr << "http read error: " << error.message() << '\n';
                return;
            }

            if (!websocket::is_upgrade(request)) {
                http::response<http::string_body> response{http::status::ok, request.version()};
                response.set(http::field::server, "chatroom-beast");
                response.set(http::field::content_type, "text/html; charset=utf-8");
                response.keep_alive(false);
                response.body() = std::string(kHomePage);
                response.prepare_payload();

                http::write(socket, response, error);
                socket.shutdown(tcp::socket::shutdown_send, error);
                return;
            }

            const std::string target(request.target().data(), request.target().size());
            const std::string room_id = extract_room_id(target);
            auto session = std::make_shared<WebSocketSession>(std::move(socket), *this, room_id);
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                rooms_[room_id].push_back(session);
            }

            session->run(std::move(request));
        }).detach();
    }
}

void WebSocketServer::handle_client_message(const std::string& room_id, const std::string& message) {
    if (message_repository_) {
        message_repository_->save_message(room_id, message);
    }

    std::cout << "client message in room: " << room_id << '\n';
    broadcast_local(room_id, message);

    if (message_bus_ && message_bus_->available()) {
        message_bus_->publish({room_id, message});
    }
}

void WebSocketServer::broadcast_local(const std::string& room_id, const std::string& message) {
    std::vector<std::shared_ptr<WebSocketSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto room = rooms_.find(room_id);
        if (room == rooms_.end()) {
            return;
        }

        auto& room_sessions = room->second;
        room_sessions.erase(
            std::remove_if(room_sessions.begin(), room_sessions.end(), [](const auto& session) {
                return session.expired();
            }),
            room_sessions.end());

        sessions.reserve(room_sessions.size());
        for (const auto& weak_session : room_sessions) {
            if (auto session = weak_session.lock()) {
                sessions.push_back(session);
            }
        }
    }

    std::cout << "broadcasting to " << sessions.size() << " local client(s) in room: " << room_id << '\n';
    for (const auto& session : sessions) {
        session->enqueue_text(message);
    }
}

void WebSocketServer::broadcast_stats(const std::string& room_id) {
    broadcast_local(room_id, make_control_stats(stats_for_room(room_id)));
}

void WebSocketServer::broadcast_all_stats() {
    std::vector<std::string> room_ids;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        room_ids.reserve(rooms_.size());
        for (const auto& [room_id, _] : rooms_) {
            room_ids.push_back(room_id);
        }
    }

    for (const auto& room_id : room_ids) {
        broadcast_stats(room_id);
    }
}

ServerStats WebSocketServer::stats_for_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    ServerStats stats;

    for (auto& [current_room_id, room_sessions] : rooms_) {
        room_sessions.erase(
            std::remove_if(room_sessions.begin(), room_sessions.end(), [](const auto& session) {
                return session.expired();
            }),
            room_sessions.end());

        stats.online_count += room_sessions.size();
        if (current_room_id == room_id) {
            stats.room_count = room_sessions.size();
        }
    }

    return stats;
}

std::vector<std::string> WebSocketServer::load_recent_messages(const std::string& room_id) {
    if (!message_repository_) {
        return {};
    }

    return message_repository_->load_recent_messages(room_id);
}

void WebSocketServer::remove_session(const std::shared_ptr<WebSocketSession>& session) {
    std::string removed_room_id;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        for (auto room = rooms_.begin(); room != rooms_.end();) {
            auto& room_sessions = room->second;
            const std::size_t old_size = room_sessions.size();
            room_sessions.erase(
                std::remove_if(room_sessions.begin(), room_sessions.end(), [&session](const auto& weak_session) {
                    const auto current = weak_session.lock();
                    return !current || current == session;
                }),
                room_sessions.end());

            if (old_size != room_sessions.size() && removed_room_id.empty()) {
                removed_room_id = room->first;
            }

            if (room_sessions.empty()) {
                room = rooms_.erase(room);
            } else {
                ++room;
            }
        }
    }

    if (!removed_room_id.empty()) {
        broadcast_all_stats();
    }
}

} // namespace chatroom
