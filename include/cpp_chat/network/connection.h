#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::network {

// 网络连接在业务层中的稳定标识。
// socket fd 只作为系统句柄使用，ConnectionId 由服务端自增生成，避免 fd 复用
// 导致旧异步响应误发给新连接。
using ConnectionId = std::uint64_t;

// 连接元信息结构，预留给后续记录 peer 地址、认证状态等网络层信息。
struct Connection {
    ConnectionId id = 0;
    std::string peer_address;
};

} // namespace cpp_chat::network
