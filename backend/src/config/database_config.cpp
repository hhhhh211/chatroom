#include "chatroom/config/database_config.h"

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

unsigned int read_env_uint(const char* name, unsigned int fallback) {
    if (const char* value = std::getenv(name)) {
        const int parsed = std::atoi(value);
        if (parsed > 0) {
            return static_cast<unsigned int>(parsed);
        }
    }

    return fallback;
}

} // namespace

DatabaseConfig DatabaseConfig::from_environment() {
    DatabaseConfig config;
    config.host = read_env_string("CHATROOM_MYSQL_HOST", config.host);
    config.port = read_env_port("CHATROOM_MYSQL_PORT", config.port);
    config.user = read_env_string("CHATROOM_MYSQL_USER", config.user);
    config.password = read_env_string("CHATROOM_MYSQL_PASSWORD", config.password);
    config.database = read_env_string("CHATROOM_MYSQL_DATABASE", config.database);
    config.history_limit = read_env_uint("CHATROOM_HISTORY_LIMIT", config.history_limit);
    return config;
}

} // namespace chatroom
