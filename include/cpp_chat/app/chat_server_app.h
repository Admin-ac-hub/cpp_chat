#pragma once

#include "cpp_chat/chat/chat_service.h"
#include "cpp_chat/core/server_config.h"
#include "cpp_chat/logging/logger.h"
#include "cpp_chat/network/tcp_server.h"
#include "cpp_chat/session/session_manager.h"
#include "cpp_chat/storage/message_store.h"

namespace cpp_chat::app {

class ChatServerApp {
public:
    explicit ChatServerApp(core::ServerConfig config);

    int run();

private:
    core::ServerConfig config_;
    logging::Logger logger_;
    storage::MessageStore message_store_;
    session::SessionManager session_manager_;
    chat::ChatService chat_service_;
    network::TcpServer tcp_server_;
};

} // namespace cpp_chat::app

