#include "cpp_chat/app/chat_server_app.h"

namespace cpp_chat::app {

ChatServerApp::ChatServerApp(core::ServerConfig config)
    : config_(std::move(config)),
      chat_service_(session_manager_, message_store_, logger_),
      tcp_server_(config_, logger_) {}

int ChatServerApp::run() {
    logger_.info("starting chat server");
    tcp_server_.start();
    logger_.info("chat server stopped");
    return 0;
}

} // namespace cpp_chat::app

