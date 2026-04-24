#include "cpp_chat/session/session_manager.h"

namespace cpp_chat::session {

void SessionManager::bind(Session session) {
    const auto existing_user = sessions_.find(session.user_id);
    if (existing_user != sessions_.end()) {
        users_by_connection_.erase(existing_user->second.connection_id);
    }

    const auto existing_connection = users_by_connection_.find(session.connection_id);
    if (existing_connection != users_by_connection_.end()) {
        sessions_.erase(existing_connection->second);
    }

    users_by_connection_[session.connection_id] = session.user_id;
    sessions_[session.user_id] = std::move(session);
}

void SessionManager::unbind(UserId user_id) {
    const auto it = sessions_.find(user_id);
    if (it != sessions_.end()) {
        users_by_connection_.erase(it->second.connection_id);
    }
    sessions_.erase(user_id);
}

void SessionManager::unbind_connection(network::ConnectionId connection_id) {
    const auto it = users_by_connection_.find(connection_id);
    if (it == users_by_connection_.end()) {
        return;
    }

    sessions_.erase(it->second);
    users_by_connection_.erase(it);
}

std::optional<Session> SessionManager::find(UserId user_id) const {
    const auto it = sessions_.find(user_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<Session> SessionManager::find_by_connection(network::ConnectionId connection_id) const {
    const auto user_it = users_by_connection_.find(connection_id);
    if (user_it == users_by_connection_.end()) {
        return std::nullopt;
    }

    return find(user_it->second);
}

} // namespace cpp_chat::session
