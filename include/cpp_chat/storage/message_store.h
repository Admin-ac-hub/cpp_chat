#pragma once

#include "cpp_chat/protocol/message.h"

#include <vector>

namespace cpp_chat::storage {

class MessageStore {
public:
    void append(protocol::Message message);
    std::vector<protocol::Message> all() const;

private:
    std::vector<protocol::Message> messages_;
};

} // namespace cpp_chat::storage

