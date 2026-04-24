#pragma once

#include "cpp_chat/logging/logger.h"
#include "cpp_chat/protocol/message.h"
#include "cpp_chat/session/session_manager.h"
#include "cpp_chat/storage/message_store.h"

namespace cpp_chat::chat {

class ChatService {
public:
    ChatService(session::SessionManager& sessions,
                storage::MessageStore& message_store,
                logging::Logger& logger);

    void handle_message(const protocol::Message& message);

private:
    session::SessionManager& sessions_;
    storage::MessageStore& message_store_;
    logging::Logger& logger_;
};

} // namespace cpp_chat::chat

