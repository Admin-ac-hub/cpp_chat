#pragma once

#include "cpp_chat/core/server_config.h"
#include "cpp_chat/logging/logger.h"

#include <string>
#include <unordered_map>

namespace cpp_chat::chat {
class ChatService;
}

namespace cpp_chat::network {

class TcpServer {
public:
    TcpServer(const core::ServerConfig& config, chat::ChatService& chat_service, logging::Logger& logger);
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
    bool handle_client_line(int client_fd, const std::string& line);
    bool send_to_client(int client_fd, const std::string& data);
    void close_client(int client_fd);

    int listen_fd_ = -1;
    int epoll_fd_ = -1;

    const core::ServerConfig& config_;
    chat::ChatService& chat_service_;
    logging::Logger& logger_;
    std::unordered_map<int, std::string> read_buffers_;
    bool running_ = false;
};

} // namespace cpp_chat::network
