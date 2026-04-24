#include "cpp_chat/chat/chat_service.h"

namespace cpp_chat::chat {

ChatService::ChatService(session::SessionManager& sessions,
                         storage::MessageStore& message_store,
                         logging::Logger& logger)
    : sessions_(sessions), message_store_(message_store), logger_(logger) {}

void ChatService::handle_message(const protocol::Message& message) {
    message_store_.append(message);
    logger_.info("stored message type=" + protocol::to_string(message.type));
}

} // namespace cpp_chat::chat

