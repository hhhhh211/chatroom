#include "chatroom/message/message_bus.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifdef CHATROOM_ENABLE_REDIS
#include <hiredis/hiredis.h>
#endif

namespace chatroom {
namespace {

#ifdef CHATROOM_ENABLE_REDIS

std::string make_origin_id() {
    std::random_device device;
    std::mt19937_64 generator(device());
    std::uniform_int_distribution<unsigned long long> distribution;

    std::ostringstream stream;
    stream << std::hex << distribution(generator) << distribution(generator);
    return stream.str();
}

std::string encode_part(std::string_view value) {
    return std::to_string(value.size()) + ":" + std::string(value);
}

bool read_part(const std::string& payload, std::size_t& offset, std::string& value) {
    const std::size_t divider = payload.find(':', offset);
    if (divider == std::string::npos) {
        return false;
    }

    std::size_t part_size = 0;
    try {
        part_size = static_cast<std::size_t>(std::stoul(payload.substr(offset, divider - offset)));
    } catch (const std::exception&) {
        return false;
    }

    const std::size_t part_start = divider + 1U;
    if (part_start + part_size > payload.size()) {
        return false;
    }

    value = payload.substr(part_start, part_size);
    offset = part_start + part_size;
    return true;
}

std::string encode_message(const std::string& origin_id, const BusMessage& message) {
    return encode_part(origin_id) + encode_part(message.room_id) + message.content;
}

bool decode_message(const std::string& payload, std::string& origin_id, BusMessage& message) {
    std::size_t offset = 0;
    if (!read_part(payload, offset, origin_id)) {
        return false;
    }
    if (!read_part(payload, offset, message.room_id)) {
        return false;
    }

    message.content = payload.substr(offset);
    return !message.room_id.empty();
}

std::string redis_error(redisContext* context) {
    if (context == nullptr || context->errstr == nullptr) {
        return "unknown redis error";
    }

    return context->errstr;
}

void authenticate_if_needed(redisContext* context, const RedisConfig& config) {
    if (config.password.empty()) {
        return;
    }

    redisReply* raw_reply = static_cast<redisReply*>(redisCommand(context, "AUTH %s", config.password.c_str()));
    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(raw_reply, freeReplyObject);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        throw std::runtime_error("redis auth failed");
    }
}

#endif

} // namespace

#ifndef CHATROOM_ENABLE_REDIS

class MessageBus::Impl {
public:
    explicit Impl(RedisConfig config) : config_(std::move(config)) {}

    bool available() const {
        return false;
    }

    void start(MessageHandler) {
        std::cerr << "Redis Pub/Sub is disabled because hiredis was not found at configure time.\n";
        std::cerr << "Install hiredis development headers, rerun CMake, then restart the backend.\n";
    }

    void publish(const BusMessage&) {}

    void stop() {}

private:
    RedisConfig config_;
};

#else

class MessageBus::Impl {
public:
    explicit Impl(RedisConfig config) : config_(std::move(config)), origin_id_(make_origin_id()) {}

    ~Impl() {
        stop();
    }

    bool available() const {
        return available_;
    }

    void start(MessageHandler handler) {
        handler_ = std::move(handler);

        try {
            publish_context_ = connect();
            subscribe_context_ = connect();
            subscribe();
            available_ = true;

            worker_ = std::thread([this] {
                listen_loop();
            });

            std::cout << "Redis Pub/Sub enabled: " << config_.channel << '@' << config_.host << ':' << config_.port
                      << '\n';
        } catch (const std::exception& error) {
            available_ = false;
            std::cerr << "Redis Pub/Sub disabled: " << error.what() << '\n';
            publish_context_.reset();
            subscribe_context_.reset();
        }
    }

    void publish(const BusMessage& message) {
        if (!available_ || !publish_context_) {
            return;
        }

        const std::string payload = encode_message(origin_id_, message);
        std::lock_guard<std::mutex> lock(publish_mutex_);
        redisReply* raw_reply = static_cast<redisReply*>(
            redisCommand(publish_context_.get(), "PUBLISH %s %b", config_.channel.c_str(), payload.data(), payload.size()));
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(raw_reply, freeReplyObject);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            std::cerr << "failed to publish redis message\n";
            return;
        }

        std::cout << "redis published message to room: " << message.room_id << '\n';
    }

    void stop() {
        if (!stopping_.exchange(true)) {
            if (subscribe_context_) {
                redisFree(subscribe_context_.release());
            }
            if (worker_.joinable()) {
                worker_.join();
            }
            publish_context_.reset();
            available_ = false;
        }
    }

private:
    struct RedisDeleter {
        void operator()(redisContext* context) const {
            if (context != nullptr) {
                redisFree(context);
            }
        }
    };

    std::unique_ptr<redisContext, RedisDeleter> connect() {
        redisContext* context = redisConnect(config_.host.c_str(), config_.port);
        if (context == nullptr) {
            throw std::runtime_error("redisConnect returned null");
        }

        std::unique_ptr<redisContext, RedisDeleter> connection(context);
        if (context->err != 0) {
            throw std::runtime_error(redis_error(context));
        }

        authenticate_if_needed(context, config_);
        return connection;
    }

    void subscribe() {
        redisReply* raw_reply =
            static_cast<redisReply*>(redisCommand(subscribe_context_.get(), "SUBSCRIBE %s", config_.channel.c_str()));
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(raw_reply, freeReplyObject);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            throw std::runtime_error("redis subscribe failed");
        }
    }

    void listen_loop() {
        while (!stopping_) {
            void* raw_reply = nullptr;
            if (redisGetReply(subscribe_context_.get(), &raw_reply) != REDIS_OK) {
                if (!stopping_) {
                    std::cerr << "redis subscribe error: " << redis_error(subscribe_context_.get()) << '\n';
                }
                break;
            }

            std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
                static_cast<redisReply*>(raw_reply), freeReplyObject);
            if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements < 3U) {
                continue;
            }

            const redisReply* kind = reply->element[0];
            const redisReply* payload = reply->element[2];
            if (kind == nullptr || payload == nullptr || kind->str == nullptr || payload->str == nullptr) {
                continue;
            }

            if (std::string(kind->str, kind->len) != "message") {
                continue;
            }

            std::string origin_id;
            BusMessage message;
            if (decode_message(std::string(payload->str, payload->len), origin_id, message) && handler_) {
                if (origin_id == origin_id_) {
                    continue;
                }
                std::cout << "redis received message for room: " << message.room_id << '\n';
                handler_(message);
            }
        }

        available_ = false;
    }

    RedisConfig config_;
    std::string origin_id_;
    MessageHandler handler_;
    std::unique_ptr<redisContext, RedisDeleter> publish_context_;
    std::unique_ptr<redisContext, RedisDeleter> subscribe_context_;
    std::thread worker_;
    std::mutex publish_mutex_;
    std::atomic_bool stopping_{false};
    std::atomic_bool available_{false};
};

#endif

MessageBus::MessageBus(RedisConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

MessageBus::~MessageBus() = default;

bool MessageBus::available() const {
    return impl_->available();
}

void MessageBus::start(MessageHandler handler) {
    impl_->start(std::move(handler));
}

void MessageBus::publish(const BusMessage& message) {
    impl_->publish(message);
}

void MessageBus::stop() {
    impl_->stop();
}

} // namespace chatroom
