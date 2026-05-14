#include "cpp_chat/core/server_config.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

namespace cpp_chat::core {

namespace {

// 默认配置文件路径，相对当前工作目录读取。
constexpr const char* kDefaultMySqlConfigFile = "config/mysql.env";

// 去掉配置文件键和值两侧的空白字符。
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// 读取 KEY=VALUE 格式配置文件。
//
// 空行、注释行和没有 '=' 的行会被忽略，避免配置文件中的说明文字影响启动。
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

// 从配置文件结果中读取值；缺失或空值时保留默认值。
std::string get_file_value_or_default(const std::unordered_map<std::string, std::string>& values,
                                      const std::string& name,
                                      std::string default_value) {
    const auto it = values.find(name);
    if (it == values.end() || it->second.empty()) {
        return default_value;
    }
    return it->second;
}

// 从真实环境变量读取值；环境变量优先级高于配置文件。
std::string get_env_or_default(const char* name, std::string default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }
    return value;
}

// 端口必须在 1-65535 范围内；解析失败时回退默认值。
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

// 环境变量版本的端口读取，复用统一的范围校验。
std::uint16_t get_env_port_or_default(const char* name, std::uint16_t default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }

    return parse_port_or_default(value, default_value);
}

int parse_positive_int_or_default(const std::string& value, int default_value) {
    if (value.empty()) {
        return default_value;
    }

    try {
        const int parsed = std::stoi(value);
        if (parsed > 0) {
            return parsed;
        }
    } catch (...) {
    }

    return default_value;
}

int parse_non_negative_int_or_default(const std::string& value, int default_value) {
    if (value.empty()) {
        return default_value;
    }

    try {
        const int parsed = std::stoi(value);
        if (parsed >= 0) {
            return parsed;
        }
    } catch (...) {
    }

    return default_value;
}

int get_env_positive_int_or_default(const char* name, int default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }

    return parse_positive_int_or_default(value, default_value);
}

int get_env_non_negative_int_or_default(const char* name, int default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }

    return parse_non_negative_int_or_default(value, default_value);
}

} // namespace

ServerConfig load_default_config() {
    ServerConfig config;
    // 先读取文件配置，再读取环境变量，使环境变量适合容器和部署时覆盖。
    const auto file_values = load_env_file(kDefaultMySqlConfigFile);

    // 文件配置覆盖结构体默认值。
    config.log_db_host = get_file_value_or_default(
        file_values, "CPP_CHAT_DB_HOST", config.log_db_host);
    config.log_db_host = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_HOST", config.log_db_host);
    config.log_db_port = parse_port_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_PORT", ""), config.log_db_port);
    config.log_db_port = parse_port_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_LOG_DB_PORT", ""), config.log_db_port);
    config.log_db_user = get_file_value_or_default(
        file_values, "CPP_CHAT_DB_USER", config.log_db_user);
    config.log_db_user = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_USER", config.log_db_user);
    config.log_db_password = get_file_value_or_default(
        file_values, "CPP_CHAT_DB_PASSWORD", config.log_db_password);
    config.log_db_password = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_PASSWORD", config.log_db_password);
    config.log_db_name = get_file_value_or_default(
        file_values, "CPP_CHAT_DB_NAME", config.log_db_name);
    config.log_db_name = get_file_value_or_default(
        file_values, "CPP_CHAT_LOG_DB_NAME", config.log_db_name);
    config.db_pool_size = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_POOL_SIZE", ""),
        config.db_pool_size);
    config.db_connect_timeout_seconds = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_CONNECT_TIMEOUT_SECONDS", ""),
        config.db_connect_timeout_seconds);
    config.db_read_timeout_seconds = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_READ_TIMEOUT_SECONDS", ""),
        config.db_read_timeout_seconds);
    config.db_write_timeout_seconds = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_WRITE_TIMEOUT_SECONDS", ""),
        config.db_write_timeout_seconds);
    config.db_acquire_timeout_ms = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_ACQUIRE_TIMEOUT_MS", ""),
        config.db_acquire_timeout_ms);
    config.db_max_reconnect_attempts = parse_non_negative_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_MAX_RECONNECT_ATTEMPTS", ""),
        config.db_max_reconnect_attempts);
    config.db_idle_ping_interval_seconds = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_IDLE_PING_INTERVAL_SECONDS", ""),
        config.db_idle_ping_interval_seconds);
    config.db_idle_check_interval_seconds = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_DB_IDLE_CHECK_INTERVAL_SECONDS", ""),
        config.db_idle_check_interval_seconds);
    config.max_read_buffer_bytes = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_MAX_READ_BUFFER_BYTES", ""),
        config.max_read_buffer_bytes);
    config.max_write_buffer_bytes = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_MAX_WRITE_BUFFER_BYTES", ""),
        config.max_write_buffer_bytes);
    config.max_response_queue_size = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_MAX_RESPONSE_QUEUE_SIZE", ""),
        config.max_response_queue_size);
    config.max_thread_pool_queue_size = parse_positive_int_or_default(
        get_file_value_or_default(file_values, "CPP_CHAT_THREAD_POOL_MAX_QUEUE_SIZE", ""),
        config.max_thread_pool_queue_size);

    // 环境变量拥有最高优先级，方便在不改文件的情况下切换数据库。
    config.log_db_host = get_env_or_default("CPP_CHAT_DB_HOST", config.log_db_host);
    config.log_db_host = get_env_or_default("CPP_CHAT_LOG_DB_HOST", config.log_db_host);
    config.log_db_port = get_env_port_or_default("CPP_CHAT_DB_PORT", config.log_db_port);
    config.log_db_port = get_env_port_or_default("CPP_CHAT_LOG_DB_PORT", config.log_db_port);
    config.log_db_user = get_env_or_default("CPP_CHAT_DB_USER", config.log_db_user);
    config.log_db_user = get_env_or_default("CPP_CHAT_LOG_DB_USER", config.log_db_user);
    config.log_db_password = get_env_or_default("CPP_CHAT_DB_PASSWORD", config.log_db_password);
    config.log_db_password = get_env_or_default("CPP_CHAT_LOG_DB_PASSWORD", config.log_db_password);
    config.log_db_name = get_env_or_default("CPP_CHAT_DB_NAME", config.log_db_name);
    config.log_db_name = get_env_or_default("CPP_CHAT_LOG_DB_NAME", config.log_db_name);
    config.db_pool_size = get_env_positive_int_or_default(
        "CPP_CHAT_DB_POOL_SIZE", config.db_pool_size);
    config.db_connect_timeout_seconds = get_env_positive_int_or_default(
        "CPP_CHAT_DB_CONNECT_TIMEOUT_SECONDS", config.db_connect_timeout_seconds);
    config.db_read_timeout_seconds = get_env_positive_int_or_default(
        "CPP_CHAT_DB_READ_TIMEOUT_SECONDS", config.db_read_timeout_seconds);
    config.db_write_timeout_seconds = get_env_positive_int_or_default(
        "CPP_CHAT_DB_WRITE_TIMEOUT_SECONDS", config.db_write_timeout_seconds);
    config.db_acquire_timeout_ms = get_env_positive_int_or_default(
        "CPP_CHAT_DB_ACQUIRE_TIMEOUT_MS", config.db_acquire_timeout_ms);
    config.db_max_reconnect_attempts = get_env_non_negative_int_or_default(
        "CPP_CHAT_DB_MAX_RECONNECT_ATTEMPTS", config.db_max_reconnect_attempts);
    config.db_idle_ping_interval_seconds = get_env_positive_int_or_default(
        "CPP_CHAT_DB_IDLE_PING_INTERVAL_SECONDS", config.db_idle_ping_interval_seconds);
    config.db_idle_check_interval_seconds = get_env_positive_int_or_default(
        "CPP_CHAT_DB_IDLE_CHECK_INTERVAL_SECONDS", config.db_idle_check_interval_seconds);
    config.max_read_buffer_bytes = get_env_positive_int_or_default(
        "CPP_CHAT_MAX_READ_BUFFER_BYTES", config.max_read_buffer_bytes);
    config.max_write_buffer_bytes = get_env_positive_int_or_default(
        "CPP_CHAT_MAX_WRITE_BUFFER_BYTES", config.max_write_buffer_bytes);
    config.max_response_queue_size = get_env_positive_int_or_default(
        "CPP_CHAT_MAX_RESPONSE_QUEUE_SIZE", config.max_response_queue_size);
    config.max_thread_pool_queue_size = get_env_positive_int_or_default(
        "CPP_CHAT_THREAD_POOL_MAX_QUEUE_SIZE", config.max_thread_pool_queue_size);
    return config;
}

} // namespace cpp_chat::core
