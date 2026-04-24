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

std::vector<OutboundMessage> ChatService::handle_client_line(network::ConnectionId connection_id,
                                                             const std::string& line) {
    protocol::ClientCommand command;
    std::string error;
    if (!protocol::parse_client_command(line, command, error)) {
        return {{connection_id, protocol::format_error(error)}};
    }

    if (command.type == protocol::MessageType::Login) {
        sessions_.bind({
            command.user_id,
            connection_id,
            command.username,
        });
        logger_.info("user logged in user_id=" + std::to_string(command.user_id) +
                     " connection_id=" + std::to_string(connection_id));
        return {{connection_id, protocol::format_ok("login")}};
    }

    if (command.type == protocol::MessageType::DirectChat) {
        const auto sender = sessions_.find_by_connection(connection_id);
        if (!sender.has_value()) {
            return {{connection_id, protocol::format_error("login required")}};
        }

        const protocol::Message message{
            protocol::MessageType::DirectChat,
            sender->user_id,
            command.receiver_id,
            command.body,
        };
        message_store_.append(message);

        const auto receiver = sessions_.find(command.receiver_id);
        if (!receiver.has_value()) {
            logger_.info("direct message stored for offline user_id=" +
                         std::to_string(command.receiver_id));
            return {{connection_id, protocol::format_error("receiver offline")}};
        }

        logger_.info("direct message from user_id=" + std::to_string(sender->user_id) +
                     " to user_id=" + std::to_string(command.receiver_id));
        return {
            {receiver->connection_id, protocol::format_direct_message(sender->user_id, command.body)},
            {connection_id, protocol::format_ok("sent")},
        };
    }

    return {{connection_id, protocol::format_error("unsupported command")}};
}

void ChatService::handle_disconnect(network::ConnectionId connection_id) {
    sessions_.unbind_connection(connection_id);
}

} // namespace cpp_chat::chat
