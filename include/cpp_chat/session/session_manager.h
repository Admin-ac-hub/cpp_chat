#pragma once

#include "cpp_chat/session/session.h"

#include <optional>
#include <unordered_map>

namespace cpp_chat::session {

class SessionManager {
public:
    void bind(Session session);
    void unbind(UserId user_id);
    std::optional<Session> find(UserId user_id) const;

private:
    std::unordered_map<UserId, Session> sessions_;
};

} // namespace cpp_chat::session

