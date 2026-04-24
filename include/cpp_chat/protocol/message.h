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

struct ClientCommand {
    MessageType type = MessageType::System;
    std::uint64_t user_id = 0;
    std::uint64_t receiver_id = 0;
    std::string username;
    std::string body;
};

std::string to_string(MessageType type);
bool parse_client_command(const std::string& line, ClientCommand& command, std::string& error);
std::string format_ok(const std::string& message);
std::string format_error(const std::string& message);
std::string format_direct_message(std::uint64_t sender_id, const std::string& body);

} // namespace cpp_chat::protocol
