#pragma once

#include "cpp_chat/logging/logger.h"
#include "cpp_chat/network/connection.h"
#include "cpp_chat/protocol/message.h"
#include "cpp_chat/session/session_manager.h"
#include "cpp_chat/storage/message_store.h"

#include <string>
#include <vector>

namespace cpp_chat::chat {

struct OutboundMessage {
    network::ConnectionId connection_id = 0;
    std::string data;
};

class ChatService {
public:
    ChatService(session::SessionManager& sessions,
                storage::MessageStore& message_store,
                logging::Logger& logger);

    void handle_message(const protocol::Message& message);
    std::vector<OutboundMessage> handle_client_line(network::ConnectionId connection_id,
                                                    const std::string& line);
    void handle_disconnect(network::ConnectionId connection_id);

private:
    session::SessionManager& sessions_;
    storage::MessageStore& message_store_;
    logging::Logger& logger_;
};

} // namespace cpp_chat::chat
