#pragma once

#include "cpp_chat/network/connection.h"

#include <cstdint>
#include <string>

namespace cpp_chat::session {

using UserId = std::uint64_t;

struct Session {
    UserId user_id = 0;
    network::ConnectionId connection_id = 0;
    std::string username;
};

} // namespace cpp_chat::session

