#include "cpp_chat/chat/chat_service.h"

#include <algorithm>

namespace cpp_chat::chat {

namespace {

constexpr std::uint32_t kDefaultHistoryLimit = 20;
constexpr std::uint32_t kMaxHistoryLimit = 100;

std::uint32_t normalize_history_limit(std::uint32_t limit) {
    if (limit == 0) {
        return kDefaultHistoryLimit;
    }
    return std::min(limit, kMaxHistoryLimit);
}

} // namespace

ChatService::ChatService(session::SessionManager& sessions,
                         storage::MessageStore& message_store,
                         storage::UserStore& user_store,
                         storage::GroupStore& group_store,
                         storage::GroupMemberStore& group_member_store,
                         logging::Logger& logger)
    : sessions_(sessions),
      message_store_(message_store),
      user_store_(user_store),
      group_store_(group_store),
      group_member_store_(group_member_store),
      logger_(logger) {}

void ChatService::handle_message(const protocol::Message& message) {
    message_store_.append(message);
    logger_.info("stored message type=" + protocol::to_string(message.type));
}

std::vector<OutboundMessage> ChatService::handle_client_line(network::ConnectionId connection_id,
                                                             const std::string& line) {
    protocol::ClientCommand command;
    std::string error;
    if (!protocol::parse_client_command(line, command, error)) {
        logger_.warn("invalid client command connection_id=" + std::to_string(connection_id) +
                     " error=" + error);
        return {{connection_id, protocol::format_error(error)}};
    }

    if (command.type == protocol::MessageType::Ping) {
        return {{connection_id, protocol::format_pong()}};
    }

    if (command.type == protocol::MessageType::Register) {
        if (sessions_.find_by_connection(connection_id).has_value()) {
            return {{connection_id, protocol::format_error("already logged in")}};
        }
        const auto result = user_store_.register_user(command.username, command.password);
        if (!result.success) {
            return {{connection_id, protocol::format_register_failed(result.reason)}};
        }
        logger_.info("user registered username=" + command.username +
                     " user_id=" + std::to_string(result.user_id));
        return {{connection_id, protocol::format_register_success(result.user_id)}};
    }

    if (command.type == protocol::MessageType::Login) {
        if (sessions_.find_by_connection(connection_id).has_value()) {
            return {{connection_id, protocol::format_login_failed("already logged in")}};
        }
        if (sessions_.is_username_online(command.username)) {
            return {{connection_id, protocol::format_login_failed("user already online")}};
        }

        storage::UserRecord user;
        if (!user_store_.verify_login(command.username, command.password, user)) {
            return {{connection_id, protocol::format_login_failed("invalid username or password")}};
        }

        sessions_.bind({
            user.id,
            connection_id,
            user.username,
        });
        logger_.info("user logged in username=" + user.username +
                     " user_id=" + std::to_string(user.id) +
                     " connection_id=" + std::to_string(connection_id));
        return {{connection_id, protocol::format_login_success(user.id, user.username)}};
    }

    const auto sender = sessions_.find_by_connection(connection_id);
    if (!sender.has_value()) {
        return {{connection_id, protocol::format_error("please login first")}};
    }

    if (command.type == protocol::MessageType::Logout) {
        sessions_.unbind_connection(connection_id);
        logger_.info("user logged out username=" + sender->username +
                     " connection_id=" + std::to_string(connection_id));
        return {{connection_id, protocol::format_ok("logout"), true}};
    }

    if (command.type == protocol::MessageType::CreateGroup) {
        const auto result = group_store_.create_group(command.group_name, sender->user_id);
        if (!result.success) {
            return {{connection_id, protocol::format_error(result.reason)}};
        }
        const auto join_result = group_member_store_.join_group(result.group.id, sender->user_id);
        if (!join_result.success) {
            return {{connection_id, protocol::format_error(join_result.reason)}};
        }
        sessions_.join_group(result.group.id, sender->user_id);
        logger_.info("group created group_id=" + std::to_string(result.group.id) +
                     " name=" + result.group.name +
                     " owner=" + sender->username);
        return {{connection_id,
                 protocol::format_create_group_success(result.group.id, result.group.name)}};
    }

    if (command.type == protocol::MessageType::GroupJoin) {
        const auto group_id = command.group_id;
        const auto result = group_member_store_.join_group(group_id, sender->user_id);
        if (!result.success) {
            return {{connection_id, protocol::format_error(result.reason)}};
        }
        sessions_.join_group(group_id, sender->user_id);
        logger_.info("user joined group username=" + sender->username +
                     " group_id=" + std::to_string(group_id));
        return {{connection_id, protocol::format_ok("joined group " + std::to_string(group_id))}};
    }

    if (command.type == protocol::MessageType::GroupLeave) {
        const auto group_id = command.group_id;
        const auto result = group_member_store_.leave_group(group_id, sender->user_id);
        if (!result.success) {
            return {{connection_id, protocol::format_error(result.reason)}};
        }
        sessions_.leave_group(group_id, sender->user_id);
        logger_.info("user left group username=" + sender->username +
                     " group_id=" + std::to_string(group_id));
        return {{connection_id, protocol::format_ok("left group " + std::to_string(group_id))}};
    }

    if (command.type == protocol::MessageType::GroupChat) {
        const auto group_id = command.group_id;
        if (!group_member_store_.is_group_member(group_id, sender->user_id)) {
            return {{connection_id, protocol::format_error("not a member of this group")}};
        }

        const protocol::Message message{
            protocol::MessageType::GroupChat,
            sender->user_id,
            group_id,
            command.body,
        };
        const auto stored = message_store_.append(message);

        const auto members = group_member_store_.load_group_members(group_id);
        std::vector<OutboundMessage> out;
        const auto formatted = protocol::format_group_message(group_id, sender->username, command.body);
        bool delivered = false;
        for (const auto member_id : members) {
            if (member_id == sender->user_id) {
                continue;
            }
            const auto member_session = sessions_.find(member_id);
            if (member_session.has_value()) {
                out.push_back({member_session->connection_id, formatted});
                delivered = true;
            }
        }
        out.push_back({connection_id, protocol::format_message_ack(stored.id, true, delivered)});
        logger_.info("group message from username=" + sender->username +
                     " to group_id=" + std::to_string(group_id));
        return out;
    }

    if (command.type == protocol::MessageType::HistoryQuery) {
        std::vector<OutboundMessage> out;
        const auto limit = normalize_history_limit(command.limit);

        if (command.group_history) {
            const auto group_id = command.group_id;
            if (!group_member_store_.is_group_member(group_id, sender->user_id)) {
                return {{connection_id, protocol::format_error("not a member of this group")}};
            }
            auto history = message_store_.group_history_page(group_id, limit, command.before_id);
            std::reverse(history.begin(), history.end());
            std::uint64_t next_before_id = 0;
            for (const auto& msg : history) {
                if (next_before_id == 0 || msg.id < next_before_id) {
                    next_before_id = msg.id;
                }
                const auto from = user_store_.find_user_by_id(msg.sender_id);
                out.push_back({connection_id,
                    protocol::format_history_item(
                        msg.id,
                        "gc",
                        from.has_value() ? from->username : std::to_string(msg.sender_id),
                        std::to_string(msg.receiver_id),
                        msg.body,
                        msg.created_at)});
            }
            out.push_back({connection_id,
                protocol::format_history_end(history.size() == limit, next_before_id)});
            return out;
        } else {
            const auto peer = user_store_.find_user_by_username(command.target_username);
            if (!peer.has_value()) {
                return {{connection_id, protocol::format_error("peer not found")}};
            }

            auto history = message_store_.direct_history_page(
                sender->user_id, peer->id, limit, command.before_id);
            std::reverse(history.begin(), history.end());
            std::uint64_t next_before_id = 0;
            for (const auto& msg : history) {
                if (next_before_id == 0 || msg.id < next_before_id) {
                    next_before_id = msg.id;
                }
                const auto from = user_store_.find_user_by_id(msg.sender_id);
                const auto to = user_store_.find_user_by_id(msg.receiver_id);
                out.push_back({connection_id,
                    protocol::format_history_item(
                        msg.id,
                        "dm",
                        from.has_value() ? from->username : std::to_string(msg.sender_id),
                        to.has_value() ? to->username : std::to_string(msg.receiver_id),
                        msg.body,
                        msg.created_at)});
            }
            out.push_back({connection_id,
                protocol::format_history_end(history.size() == limit, next_before_id)});
            return out;
        }
    }

    if (command.type == protocol::MessageType::DirectChat) {
        const auto receiver_user = user_store_.find_user_by_username(command.target_username);
        if (!receiver_user.has_value()) {
            return {{connection_id, protocol::format_error("receiver not found")}};
        }

        const protocol::Message message{
            protocol::MessageType::DirectChat,
            sender->user_id,
            receiver_user->id,
            command.body,
        };
        const auto stored = message_store_.append(message);

        const auto receiver_session = sessions_.find_by_username(command.target_username);
        if (!receiver_session.has_value()) {
            logger_.info("direct message stored for offline username=" + command.target_username);
            return {{connection_id, protocol::format_message_ack(stored.id, true, false)}};
        }

        logger_.info("direct message from username=" + sender->username +
                     " to username=" + command.target_username);
        return {
            {receiver_session->connection_id, protocol::format_direct_message(sender->username, command.body)},
            {connection_id, protocol::format_message_ack(stored.id, true, true)},
        };
    }

    if (command.type == protocol::MessageType::UnreadQuery) {
        const auto limit = normalize_history_limit(command.limit);
        const auto group_ids = group_member_store_.load_user_groups(sender->user_id);
        auto unread = message_store_.unread_page(
            sender->user_id, group_ids, command.last_seen_message_id, limit);

        std::vector<OutboundMessage> out;
        std::uint64_t next_last_seen = command.last_seen_message_id;
        for (const auto& msg : unread) {
            next_last_seen = std::max(next_last_seen, msg.id);
            if (msg.type == protocol::MessageType::GroupChat) {
                const auto from = user_store_.find_user_by_id(msg.sender_id);
                out.push_back({connection_id,
                    protocol::format_history_item(
                        msg.id,
                        "gc",
                        from.has_value() ? from->username : std::to_string(msg.sender_id),
                        std::to_string(msg.receiver_id),
                        msg.body,
                        msg.created_at)});
            } else {
                const auto from = user_store_.find_user_by_id(msg.sender_id);
                const auto to = user_store_.find_user_by_id(msg.receiver_id);
                out.push_back({connection_id,
                    protocol::format_history_item(
                        msg.id,
                        "dm",
                        from.has_value() ? from->username : std::to_string(msg.sender_id),
                        to.has_value() ? to->username : std::to_string(msg.receiver_id),
                        msg.body,
                        msg.created_at)});
            }
        }
        out.push_back({connection_id,
            protocol::format_unread_end(unread.size() == limit, next_last_seen)});
        return out;
    }

    logger_.warn("unsupported command type=" + protocol::to_string(command.type) +
                 " connection_id=" + std::to_string(connection_id));
    return {{connection_id, protocol::format_error("unsupported command")}};
}

void ChatService::handle_disconnect(network::ConnectionId connection_id) {
    sessions_.unbind_connection(connection_id);
}

} // namespace cpp_chat::chat
