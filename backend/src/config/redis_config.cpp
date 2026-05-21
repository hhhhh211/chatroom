#include "chatroom/config/redis_config.h"

#include <cstdlib>

namespace chatroom {
namespace {

std::string read_env_string(const char* name, std::string fallback) {
    if (const char* value = std::getenv(name)) {
        if (*value != '\0') {
            return value;
        }
    }

    return fallback;
}

std::uint16_t read_env_port(const char* name, std::uint16_t fallback) {
    if (const char* value = std::getenv(name)) {
        const int parsed = std::atoi(value);
        if (parsed > 0 && parsed <= 65535) {
            return static_cast<std::uint16_t>(parsed);
        }
    }

    return fallback;
}

} // namespace

RedisConfig RedisConfig::from_environment() {
    RedisConfig config;
    config.host = read_env_string("CHATROOM_REDIS_HOST", config.host);
    config.port = read_env_port("CHATROOM_REDIS_PORT", config.port);
    config.password = read_env_string("CHATROOM_REDIS_PASSWORD", config.password);
    config.channel = read_env_string("CHATROOM_REDIS_CHANNEL", config.channel);
    return config;
}

} // namespace chatroom
