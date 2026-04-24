#include "cpp_chat/protocol/message.h"

#include <sstream>

namespace cpp_chat::protocol {

namespace {

void trim_trailing_cr(std::string& value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
}

void trim_leading_space(std::string& value) {
    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
}

} // namespace

std::string to_string(MessageType type) {
    switch (type) {
        case MessageType::Login:
            return "login";
        case MessageType::DirectChat:
            return "direct_chat";
        case MessageType::GroupChat:
            return "group_chat";
        case MessageType::HistoryQuery:
            return "history_query";
        case MessageType::System:
            return "system";
    }
    return "unknown";
}

bool parse_client_command(const std::string& line, ClientCommand& command, std::string& error) {
    std::string normalized = line;
    trim_trailing_cr(normalized);

    std::istringstream input(normalized);
    std::string command_name;
    input >> command_name;

    if (command_name == "LOGIN") {
        command.type = MessageType::Login;
        if (!(input >> command.user_id >> command.username)) {
            error = "usage: LOGIN <user_id> <username>";
            return false;
        }
        return true;
    }

    if (command_name == "DM") {
        command.type = MessageType::DirectChat;
        if (!(input >> command.receiver_id)) {
            error = "usage: DM <receiver_id> <message>";
            return false;
        }

        std::getline(input, command.body);
        trim_leading_space(command.body);
        if (command.body.empty()) {
            error = "direct message body is empty";
            return false;
        }
        return true;
    }

    error = "unknown command";
    return false;
}

std::string format_ok(const std::string& message) {
    return "OK " + message + "\n";
}

std::string format_error(const std::string& message) {
    return "ERR " + message + "\n";
}

std::string format_direct_message(std::uint64_t sender_id, const std::string& body) {
    return "FROM " + std::to_string(sender_id) + " " + body + "\n";
}

} // namespace cpp_chat::protocol
