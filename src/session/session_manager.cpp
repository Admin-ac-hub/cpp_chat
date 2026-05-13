#include "cpp_chat/session/session_manager.h"

namespace cpp_chat::session {

void SessionManager::bind(Session session) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto existing_user = sessions_.find(session.user_id);
    if (existing_user != sessions_.end()) {
        users_by_connection_.erase(existing_user->second.connection_id);
        users_by_username_.erase(existing_user->second.username);
    }

    const auto existing_connection = users_by_connection_.find(session.connection_id);
    if (existing_connection != users_by_connection_.end()) {
        const auto old_session = sessions_.find(existing_connection->second);
        if (old_session != sessions_.end()) {
            users_by_username_.erase(old_session->second.username);
        }
        sessions_.erase(existing_connection->second);
    }

    users_by_connection_[session.connection_id] = session.user_id;
    users_by_username_[session.username] = session.user_id;
    sessions_[session.user_id] = std::move(session);
}

void SessionManager::unbind(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = sessions_.find(user_id);
    if (it != sessions_.end()) {
        users_by_connection_.erase(it->second.connection_id);
        users_by_username_.erase(it->second.username);
    }
    sessions_.erase(user_id);
}

void SessionManager::unbind_connection(network::ConnectionId connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = users_by_connection_.find(connection_id);
    if (it == users_by_connection_.end()) {
        return;
    }

    const auto session_it = sessions_.find(it->second);
    if (session_it != sessions_.end()) {
        users_by_username_.erase(session_it->second.username);
    }
    sessions_.erase(it->second);
    users_by_connection_.erase(it);
}

std::optional<Session> SessionManager::find(UserId user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = sessions_.find(user_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<Session> SessionManager::find_by_connection(network::ConnectionId connection_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto user_it = users_by_connection_.find(connection_id);
    if (user_it == users_by_connection_.end()) {
        return std::nullopt;
    }

    const auto it = sessions_.find(user_it->second);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<Session> SessionManager::find_by_username(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto user_it = users_by_username_.find(username);
    if (user_it == users_by_username_.end()) {
        return std::nullopt;
    }

    const auto session_it = sessions_.find(user_it->second);
    if (session_it == sessions_.end()) {
        return std::nullopt;
    }
    return session_it->second;
}

bool SessionManager::is_username_online(const std::string& username) const {
    return find_by_username(username).has_value();
}

void SessionManager::join_group(GroupId group_id, UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    group_members_[group_id].insert(user_id);
}

void SessionManager::leave_group(GroupId group_id, UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = group_members_.find(group_id);
    if (it == group_members_.end()) {
        return;
    }
    it->second.erase(user_id);
    if (it->second.empty()) {
        group_members_.erase(it);
    }
}

std::vector<UserId> SessionManager::get_group_members(GroupId group_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = group_members_.find(group_id);
    if (it == group_members_.end()) {
        return {};
    }
    return {it->second.begin(), it->second.end()};
}

bool SessionManager::is_group_member(GroupId group_id, UserId user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = group_members_.find(group_id);
    if (it == group_members_.end()) {
        return false;
    }
    return it->second.count(user_id) > 0;
}

} // namespace cpp_chat::session
