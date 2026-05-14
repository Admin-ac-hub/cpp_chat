#include "cpp_chat/app/chat_server_app.h"

#include <cstddef>
#include <iostream>
#include <utility>

namespace cpp_chat::app {

ChatServerApp::ChatServerApp(core::ServerConfig config)
    : config_(std::move(config)),
      db_pool_(storage::MySqlConnectionPoolConfig{
          config_.log_db_host,
          config_.log_db_port,
          config_.log_db_user,
          config_.log_db_password,
          config_.log_db_name,
          config_.db_pool_size,
          config_.db_connect_timeout_seconds,
          config_.db_read_timeout_seconds,
          config_.db_write_timeout_seconds,
          config_.db_acquire_timeout_ms,
          config_.db_max_reconnect_attempts,
          config_.db_idle_ping_interval_seconds,
          config_.db_idle_check_interval_seconds,
      }),
      logger_(db_pool_),
      message_store_(db_pool_),
      user_store_(db_pool_),
      group_store_(db_pool_),
      group_member_store_(db_pool_),
      // 线程池大小由配置控制，默认为 4。
      thread_pool_(config_.worker_threads,
                   static_cast<std::size_t>(config_.max_thread_pool_queue_size)),
      chat_service_(session_manager_, message_store_, user_store_, group_store_, group_member_store_, logger_),
      tcp_server_(config_, chat_service_, thread_pool_, logger_) {}

int ChatServerApp::run() {
    if (!db_pool_.ready()) {
        std::cerr << "[ERROR] failed to initialize MySQL connection pool" << std::endl;
        return 1;
    }

    // start 会阻塞在事件循环中，直到服务器停止或初始化失败。
    logger_.info("starting chat server");
    tcp_server_.start();
    logger_.info("chat server stopped");
    return 0;
}

} // namespace cpp_chat::app
