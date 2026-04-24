#include "cpp_chat/storage/message_store.h"

namespace cpp_chat::storage {

void MessageStore::append(protocol::Message message) {
    messages_.push_back(std::move(message));
}

std::vector<protocol::Message> MessageStore::all() const {
    return messages_;
}

} // namespace cpp_chat::storage

