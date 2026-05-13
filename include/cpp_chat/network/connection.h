#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::network {

// 网络连接在业务层中的稳定标识。
// 当前实现直接使用 socket fd 转换而来，后续如果引入连接对象池也可以替换实现。
using ConnectionId = std::uint64_t;

// 连接元信息结构，预留给后续记录 peer 地址、认证状态等网络层信息。
struct Connection {
    ConnectionId id = 0;
    std::string peer_address;
};

} // namespace cpp_chat::network
