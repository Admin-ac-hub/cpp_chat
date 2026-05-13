#pragma once

#include "cpp_chat/core/server_config.h"
#include "cpp_chat/core/thread_pool.h"
#include "cpp_chat/logging/logger.h"

#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cpp_chat::chat {
class ChatService;
}

namespace cpp_chat::network {

struct QueuedResponse {
    int fd = -1;
    std::string data;
    bool close_after_send = false;
};

// 基于 Linux epoll 的 TCP 聊天服务器。
//
// TcpServer 只负责网络 I/O：
// - 监听端口并接收连接。
// - 把客户端字节流切成按行命令。
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

    // 在工作线程中执行：调用业务层处理一行命令，将响应推入 response_queue_。
    void process_line_async(int client_fd, const std::string& line);

    // 由网络线程调用：将 response_queue_ 中所有待发送响应写回各连接。
    void drain_responses();

    bool send_to_client(int client_fd, const std::string& data);
    void flush_write_buffer(int client_fd);
    bool set_write_mode(int client_fd, bool enable);
    void close_client(int client_fd);
    void check_heartbeats();

    int listen_fd_ = -1;
    int epoll_fd_ = -1;

    const core::ServerConfig& config_;
    chat::ChatService& chat_service_;
    core::ThreadPool& thread_pool_;
    logging::Logger& logger_;

    std::unordered_map<int, std::string> read_buffers_;
    std::unordered_map<int, std::string> write_buffers_;
    std::unordered_map<int, std::time_t> last_activity_;
    std::unordered_set<int> close_after_write_;

    // 工作线程 → 网络线程的响应队列。worker 推送，reactor 排空。
    mutable std::mutex response_mutex_;
    std::vector<QueuedResponse> response_queue_;

    bool running_ = false;
};

} // namespace cpp_chat::network
