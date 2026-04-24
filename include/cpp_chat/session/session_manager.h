#pragma once

#include "cpp_chat/session/session.h"

#include <optional>
#include <unordered_map>

namespace cpp_chat::session {

class SessionManager {
public:
    void bind(Session session);
    void unbind(UserId user_id);
    void unbind_connection(network::ConnectionId connection_id);
    std::optional<Session> find(UserId user_id) const;
    std::optional<Session> find_by_connection(network::ConnectionId connection_id) const;

private:
    std::unordered_map<UserId, Session> sessions_;
    std::unordered_map<network::ConnectionId, UserId> users_by_connection_;
};

} // namespace cpp_chat::session
