#include "cpp_chat/network/tcp_server.h"

namespace cpp_chat::network {

TcpServer::TcpServer(const core::ServerConfig& config, logging::Logger& logger)
    : config_(config), logger_(logger) {}

void TcpServer::start() {
    running_ = true;
    logger_.info("tcp server listening on " + config_.host + ":" + std::to_string(config_.port));
}

void TcpServer::stop() {
    running_ = false;
    logger_.info("tcp server stopped");
}

} // namespace cpp_chat::network

