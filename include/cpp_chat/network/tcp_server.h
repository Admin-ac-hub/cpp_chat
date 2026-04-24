#pragma once

#include "cpp_chat/core/server_config.h"
#include "cpp_chat/logging/logger.h"

namespace cpp_chat::network {

class TcpServer {
public:
    TcpServer(const core::ServerConfig& config, logging::Logger& logger);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void start();
    void stop();

private:
#ifdef __linux__
    void setup_listener();
    void run_event_loop();
    void accept_connections();
    void handle_client_read(int client_fd);
    void close_client(int client_fd);

    int listen_fd_ = -1;
    int epoll_fd_ = -1;
#endif

    const core::ServerConfig& config_;
    logging::Logger& logger_;
    bool running_ = false;
};

} // namespace cpp_chat::network
