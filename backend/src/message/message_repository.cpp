#include "chatroom/message/message_repository.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef CHATROOM_ENABLE_MYSQL
#include <mysql.h>
#endif

namespace chatroom {
namespace {

struct ParsedMessage {
    std::string sender_name;
    std::string content;
};

#ifdef CHATROOM_ENABLE_MYSQL
ParsedMessage parse_message(const std::string& raw_message) {
    constexpr std::size_t max_sender_length = 64;
    const std::size_t divider = raw_message.find(": ");
    if (divider == std::string::npos || divider == 0U) {
        return {"", raw_message};
    }

    std::string sender = raw_message.substr(0, divider);
    if (sender.size() > max_sender_length) {
        sender.resize(max_sender_length);
    }

    return {std::move(sender), raw_message.substr(divider + 2U)};
}
#endif

} // namespace

#ifndef CHATROOM_ENABLE_MYSQL

class MessageRepository::Impl {
public:
    explicit Impl(DatabaseConfig config) : config_(std::move(config)) {}

    bool available() const {
        return false;
    }

    void initialize_schema() {
        std::cerr << "MySQL persistence is disabled because libmysqlclient was not found at configure time.\n";
        std::cerr << "Install MySQL client development headers, rerun CMake, then restart the backend.\n";
    }

    bool save_message(const std::string&, const std::string&) {
        return false;
    }

    std::vector<std::string> load_recent_messages(const std::string&) {
        return {};
    }

private:
    DatabaseConfig config_;
};

#else

class MessageRepository::Impl {
public:
    explicit Impl(DatabaseConfig config) : config_(std::move(config)), connection_(mysql_init(nullptr)) {
        if (connection_ == nullptr) {
            throw std::runtime_error("mysql_init failed");
        }

        mysql_options(connection_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        if (mysql_real_connect(
                connection_,
                config_.host.c_str(),
                config_.user.c_str(),
                config_.password.c_str(),
                config_.database.c_str(),
                config_.port,
                nullptr,
                0) == nullptr) {
            const std::string error = mysql_error(connection_);
            mysql_close(connection_);
            connection_ = nullptr;
            throw std::runtime_error("failed to connect MySQL: " + error);
        }
    }

    ~Impl() {
        if (connection_ != nullptr) {
            mysql_close(connection_);
        }
    }

    bool available() const {
        return connection_ != nullptr;
    }

    void initialize_schema() {
        std::lock_guard<std::mutex> lock(mutex_);
        execute(
            "CREATE TABLE IF NOT EXISTS rooms ("
            "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "room_key VARCHAR(64) NOT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id),"
            "UNIQUE KEY uk_rooms_room_key (room_key)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

        execute(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "room_id BIGINT UNSIGNED NOT NULL,"
            "sender_name VARCHAR(64) NOT NULL DEFAULT '',"
            "content TEXT NOT NULL,"
            "raw_message TEXT NOT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "PRIMARY KEY (id),"
            "KEY idx_messages_room_created (room_id, created_at, id),"
            "CONSTRAINT fk_messages_room FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

        std::cout << "MySQL persistence enabled: " << config_.database << '@' << config_.host << ':' << config_.port
                  << '\n';
    }

    bool save_message(const std::string& room_id, const std::string& raw_message) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            const unsigned long long room_pk = ensure_room_locked(room_id);
            const ParsedMessage parsed = parse_message(raw_message);

            const std::string sql =
                "INSERT INTO messages (room_id, sender_name, content, raw_message) VALUES (" +
                std::to_string(room_pk) + ", '" + escape(parsed.sender_name) + "', '" + escape(parsed.content) +
                "', '" + escape(raw_message) + "')";
            execute(sql);
            return true;
        } catch (const std::exception& error) {
            std::cerr << "failed to save message: " << error.what() << '\n';
            return false;
        }
    }

    std::vector<std::string> load_recent_messages(const std::string& room_id) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            const unsigned long long room_pk = ensure_room_locked(room_id);
            const std::string sql =
                "SELECT raw_message FROM messages WHERE room_id = " + std::to_string(room_pk) +
                " ORDER BY created_at DESC, id DESC LIMIT " + std::to_string(config_.history_limit);

            if (mysql_query(connection_, sql.c_str()) != 0) {
                throw std::runtime_error(mysql_error(connection_));
            }

            MYSQL_RES* raw_result = mysql_store_result(connection_);
            if (raw_result == nullptr) {
                throw std::runtime_error(mysql_error(connection_));
            }

            std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> result(raw_result, mysql_free_result);
            std::vector<std::string> messages;

            MYSQL_ROW row = nullptr;
            while ((row = mysql_fetch_row(result.get())) != nullptr) {
                unsigned long* lengths = mysql_fetch_lengths(result.get());
                messages.emplace_back(row[0], lengths[0]);
            }

            std::reverse(messages.begin(), messages.end());
            return messages;
        } catch (const std::exception& error) {
            std::cerr << "failed to load message history: " << error.what() << '\n';
            return {};
        }
    }

private:
    void execute(const std::string& sql) {
        if (mysql_query(connection_, sql.c_str()) != 0) {
            throw std::runtime_error(mysql_error(connection_));
        }
    }

    std::string escape(const std::string& value) {
        std::string escaped;
        escaped.resize(value.size() * 2U + 1U);
        const unsigned long length = mysql_real_escape_string(
            connection_, escaped.data(), value.data(), static_cast<unsigned long>(value.size()));
        escaped.resize(length);
        return escaped;
    }

    unsigned long long ensure_room_locked(const std::string& room_id) {
        const std::string escaped_room = escape(room_id);
        execute("INSERT INTO rooms (room_key) VALUES ('" + escaped_room +
                "') ON DUPLICATE KEY UPDATE room_key = VALUES(room_key)");

        const std::string query = "SELECT id FROM rooms WHERE room_key = '" + escaped_room + "' LIMIT 1";
        if (mysql_query(connection_, query.c_str()) != 0) {
            throw std::runtime_error(mysql_error(connection_));
        }

        MYSQL_RES* raw_result = mysql_store_result(connection_);
        if (raw_result == nullptr) {
            throw std::runtime_error(mysql_error(connection_));
        }

        std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> result(raw_result, mysql_free_result);
        MYSQL_ROW row = mysql_fetch_row(result.get());
        if (row == nullptr || row[0] == nullptr) {
            throw std::runtime_error("room id not found after upsert");
        }

        return std::strtoull(row[0], nullptr, 10);
    }

    DatabaseConfig config_;
    MYSQL* connection_ = nullptr;
    std::mutex mutex_;
};

#endif

MessageRepository::MessageRepository(DatabaseConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

MessageRepository::~MessageRepository() = default;

bool MessageRepository::available() const {
    return impl_->available();
}

void MessageRepository::initialize_schema() {
    impl_->initialize_schema();
}

bool MessageRepository::save_message(const std::string& room_id, const std::string& raw_message) {
    return impl_->save_message(room_id, raw_message);
}

std::vector<std::string> MessageRepository::load_recent_messages(const std::string& room_id) {
    return impl_->load_recent_messages(room_id);
}

} // namespace chatroom
