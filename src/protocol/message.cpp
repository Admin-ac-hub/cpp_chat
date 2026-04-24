#include "cpp_chat/protocol/message.h"

namespace cpp_chat::protocol {

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

} // namespace cpp_chat::protocol

