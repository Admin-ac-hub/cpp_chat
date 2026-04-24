#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::protocol {

enum class MessageType {
    Login,
    DirectChat,
    GroupChat,
    HistoryQuery,
    System
};

struct Message {
    MessageType type = MessageType::System;
    std::uint64_t sender_id = 0;
    std::uint64_t receiver_id = 0;
    std::string body;
};

std::string to_string(MessageType type);

} // namespace cpp_chat::protocol

