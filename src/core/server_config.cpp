#include "cpp_chat/core/server_config.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

namespace cpp_chat::core {

namespace {

constexpr const char* kDefaultMySqlConfigFile = "config/mysql.env";

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::unordered_map<std::string, std::string> load_env_file(const std::string& path) {
    std::unordered_map<std::string, std::string> values;
    std::ifstream input(path);
    if (!input) {
        return values;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const auto equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, equals_pos));
        const std::string value = trim(line.substr(equals_pos + 1));
        if (!key.empty()) {
            values[key] = value;
        }
    }

    return values;
}

std::string get_file_value_or_default(const std::unordered_map<std::string, std::string>& values,
                                      const std::string& name,
                                      std::string default_value) {
    const auto it = values.find(name);
    if (it == values.end() || it->second.empty()) {
        return default_value;
    }
    return it->second;
}

std::string get_env_or_default(const char* name, std::string default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }
    return value;
}

std::uint16_t parse_port_or_default(const std::string& value, std::uint16_t default_value) {
    if (value.empty()) {
        return default_value;
    }

    try {
        const int parsed = std::stoi(value);
        if (parsed > 0 && parsed <= 65535) {
            return static_cast<std::uint16_t>(parsed);
        }
    } catch (...) {
    }

    return default_value;
}

std::uint16_t get_env_port_or_default(const char* name, std::uint16_t default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }

    return parse_port_or_default(value, default_value);
}

} // namespace

ServerConfig load_default_config() {
    ServerConfig config;
    const auto file_values = load_env_file(kDefaultMySqlConfigFile);

    config.log_db_host = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_HOST", config.log_db_host);
    config.log_db_port = parse_port_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_LOG_DB_PORT", ""), config.log_db_port);
    config.log_db_user = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_USER", config.log_db_user);
    config.log_db_password = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_PASSWORD", config.log_db_password);
    config.log_db_name = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_NAME", config.log_db_name);

    config.log_db_host = get_env_or_default("CPP_CHAT_LOG_DB_HOST", config.log_db_host);
    config.log_db_port = get_env_port_or_default("CPP_CHAT_LOG_DB_PORT", config.log_db_port);
    config.log_db_user = get_env_or_default("CPP_CHAT_LOG_DB_USER", config.log_db_user);
    config.log_db_password = get_env_or_default("CPP_CHAT_LOG_DB_PASSWORD", config.log_db_password);
    config.log_db_name = get_env_or_default("CPP_CHAT_LOG_DB_NAME", config.log_db_name);
    return config;
}

} // namespace cpp_chat::core
