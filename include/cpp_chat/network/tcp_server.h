#pragma once

#include "cpp_chat/core/server_config.h"
#include "cpp_chat/core/thread_pool.h"
#include "cpp_chat/logging/logger.h"
#include "cpp_chat/network/connection.h"

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpp_chat::chat {
class ChatService;
}

namespace cpp_chat::network {

struct QueuedResponse {
    ConnectionId connection_id = 0;
    std::uint64_t generation = 0;
    std::string data;
    bool close_after_send = false;
};

struct QueuedClose {
    ConnectionId connection_id = 0;
    std::uint64_t generation = 0;
};

struct ConnectionState {
    ConnectionId connection_id = 0;
    int fd = -1;
    std::string read_buffer;
    std::string write_buffer;
    std::time_t last_active_at = 0;
    bool closing = false;
    std::uint64_t generation = 0;
};

// 基于 Linux epoll 的 TCP 聊天服务器。
//
// TcpServer 只负责网络 I/O：
// - 监听端口并接收连接。
// - 把客户端字节流切成长度前缀 packet。
// - 调用 ChatService 处理业务。
// - 把 ChatService 返回的响应写回目标连接。
class TcpServer {
public:
    TcpServer(const core::ServerConfig& config,
              chat::ChatService& chat_service,
              core::ThreadPool& thread_pool,
              logging::Logger& logger);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void start();
    void stop();

private:
    bool setup_listener();
    bool run_event_loop();
    void accept_connections();
    void handle_client_read(int client_fd);

    // 在工作线程中执行：调用业务层处理一个完整 payload，将响应推入 response_queue_。
    void process_packet_async(ConnectionId connection_id,
                              std::uint64_t generation,
                              const std::string& payload);

    // 由网络线程调用：将 response_queue_ 中所有待发送响应写回各连接。
    void drain_responses();
    void drain_close_requests();
    void drain_wake_events();
    void wake_event_loop();

    ConnectionState* find_connection_by_fd(int fd);
    ConnectionState* find_connection(ConnectionId connection_id);
    bool is_current_connection(ConnectionId connection_id, std::uint64_t generation) const;
    bool send_to_client(ConnectionState& connection, const std::string& data);
    void flush_write_buffer(int client_fd);
    bool set_write_mode(int client_fd, bool enable);
    void close_client(int client_fd);
    void close_connection(ConnectionId connection_id);
    void check_heartbeats();

    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    int wake_fd_ = -1;

    const core::ServerConfig& config_;
    chat::ChatService& chat_service_;
    core::ThreadPool& thread_pool_;
    logging::Logger& logger_;

    std::unordered_map<int, ConnectionId> connection_by_fd_;
    std::unordered_map<ConnectionId, ConnectionState> connections_;
    ConnectionId next_connection_id_ = 1;
    std::uint64_t next_generation_ = 1;

    // 工作线程 → 网络线程的响应队列。worker 推送，reactor 排空。
    mutable std::mutex response_mutex_;
    std::vector<QueuedResponse> response_queue_;
    std::vector<QueuedClose> close_queue_;

    bool running_ = false;
};

} // namespace cpp_chat::network
