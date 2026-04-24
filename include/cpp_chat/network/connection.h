#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::network {

using ConnectionId = std::uint64_t;

struct Connection {
    ConnectionId id = 0;
    std::string peer_address;
};

} // namespace cpp_chat::network

