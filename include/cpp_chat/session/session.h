#pragma once

#include "cpp_chat/network/connection.h"

#include <cstdint>
#include <string>

namespace cpp_chat::session {

// 业务用户 ID，与网络连接 ID 分离。
// 一个用户重新登录时可以绑定到新的连接。
using UserId = std::uint64_t;

// 在线会话信息。
// 每条记录表示一个已登录用户当前绑定到哪个 TCP 连接。
struct Session {
    UserId user_id = 0;
    network::ConnectionId connection_id = 0;
    std::string username;
};

} // namespace cpp_chat::session
