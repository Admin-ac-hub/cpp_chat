#include "cpp_chat/session/session_manager.h"

namespace cpp_chat::session {

void SessionManager::bind(Session session) {
    sessions_[session.user_id] = std::move(session);
}

void SessionManager::unbind(UserId user_id) {
    sessions_.erase(user_id);
}

std::optional<Session> SessionManager::find(UserId user_id) const {
    const auto it = sessions_.find(user_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace cpp_chat::session

