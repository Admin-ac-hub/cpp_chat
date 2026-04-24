#pragma once

#include "cpp_chat/core/server_config.h"
#include "cpp_chat/logging/logger.h"

namespace cpp_chat::network {

class TcpServer {
public:
    TcpServer(const core::ServerConfig& config, logging::Logger& logger);

    void start();
    void stop();

private:
    const core::ServerConfig& config_;
    logging::Logger& logger_;
    bool running_ = false;
};

} // namespace cpp_chat::network

