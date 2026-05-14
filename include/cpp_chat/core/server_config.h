#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::core {

// 服务器运行配置。
//
// host/port 控制 TCP 服务监听地址；log_db_* 控制日志写入的 MySQL 连接。
// 默认值面向本地开发，生产环境应通过 config/mysql.env 或环境变量覆盖。
struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 9000;
    std::string log_db_host = "127.0.0.1";
    std::uint16_t log_db_port = 3306;
    std::string log_db_user = "cpp_chat";
    std::string log_db_password = "cpp_chat";
    std::string log_db_name = "cpp_chat";
    int db_pool_size = 4;
    int db_connect_timeout_seconds = 3;
    int db_read_timeout_seconds = 5;
    int db_write_timeout_seconds = 5;
    int db_acquire_timeout_ms = 3000;
    int db_max_reconnect_attempts = 3;
    int db_idle_ping_interval_seconds = 30;
    int db_idle_check_interval_seconds = 10;
    int heartbeat_interval_seconds = 10;
    int heartbeat_timeout_seconds = 60;
    int worker_threads = 4;
    int max_read_buffer_bytes = 2 * 1024 * 1024;
    int max_write_buffer_bytes = 2 * 1024 * 1024;
    int max_response_queue_size = 10000;
    int max_thread_pool_queue_size = 10000;
};

// 加载默认配置。
//
// 合并顺序：
// 1. ServerConfig 结构体默认值。
// 2. config/mysql.env 文件。
// 3. 环境变量。
//
// 后面的来源覆盖前面的来源。
ServerConfig load_default_config();

} // namespace cpp_chat::core
