// tests/test_message_store.cpp
//
// MessageStore 单元测试（内存模式，不依赖 MySQL）。
// MessageStore 默认构造使用纯内存存储，适合脱离数据库的快速单元测试。

#include "cpp_chat/storage/message_store.h"

#include <gtest/gtest.h>

namespace cpp_chat::storage {

// ─────────────────────────────────────────────
// 初始状态
// ─────────────────────────────────────────────

TEST(MessageStore, EmptyByDefault) {
    MessageStore store;
    EXPECT_TRUE(store.all().empty());
}

// ─────────────────────────────────────────────
// append & all
// ─────────────────────────────────────────────

TEST(MessageStore, AppendSingleMessage) {
    MessageStore store;
    protocol::Message msg{
        protocol::MessageType::DirectChat,
        /*sender_id=*/1,
        /*receiver_id=*/2,
        "hello"
    };
    const auto stored = store.append(msg);

    const auto all = store.all();
    EXPECT_EQ(stored.id, 1u);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].type, protocol::MessageType::DirectChat);
    EXPECT_EQ(all[0].sender_id, 1u);
    EXPECT_EQ(all[0].receiver_id, 2u);
    EXPECT_EQ(all[0].body, "hello");
}

TEST(MessageStore, AppendMultipleMessagesPreservesOrder) {
    MessageStore store;
    store.append({protocol::MessageType::DirectChat, 1, 2, "first"});
    const auto second = store.append({protocol::MessageType::DirectChat, 2, 1, "second"});
    store.append({protocol::MessageType::DirectChat, 1, 2, "third"});

    EXPECT_EQ(second.id, 2u);
    const auto all = store.all();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].body, "first");
    EXPECT_EQ(all[1].body, "second");
    EXPECT_EQ(all[2].body, "third");
}

// ─────────────────────────────────────────────
// all() 返回的是副本，修改它不影响存储内容
// ─────────────────────────────────────────────

TEST(MessageStore, AllReturnsCopy) {
    MessageStore store;
    store.append({protocol::MessageType::DirectChat, 1, 2, "original"});

    // 拿到副本并修改。
    auto copy = store.all();
    copy[0].body = "modified";

    // 存储内的原始消息不应受影响。
    EXPECT_EQ(store.all()[0].body, "original");
}

// ─────────────────────────────────────────────
// 消息正文支持 Unicode（中文、Emoji）
// ─────────────────────────────────────────────

TEST(MessageStore, SupportsUnicodeBody) {
    MessageStore store;
    const std::string body = "你好，世界！🎉";
    store.append({protocol::MessageType::DirectChat, 1, 2, body});
    EXPECT_EQ(store.all()[0].body, body);
}

// ─────────────────────────────────────────────
// 不同消息类型均可存储
// ─────────────────────────────────────────────

TEST(MessageStore, StoresAllMessageTypes) {
    MessageStore store;
    store.append({protocol::MessageType::DirectChat, 1, 2, "dm"});
    store.append({protocol::MessageType::GroupChat,  1, 100, "group"});
    store.append({protocol::MessageType::System,     0, 0,   "system"});

    const auto all = store.all();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].type, protocol::MessageType::DirectChat);
    EXPECT_EQ(all[1].type, protocol::MessageType::GroupChat);
    EXPECT_EQ(all[2].type, protocol::MessageType::System);
}

// ─────────────────────────────────────────────
// history 查询接口（内存模式）
// ─────────────────────────────────────────────

TEST(MessageStore, DirectHistoryReturnsBothDirectionsInOrder) {
    MessageStore store;
    store.append({protocol::MessageType::DirectChat, 1, 2, "one to two"});
    store.append({protocol::MessageType::DirectChat, 3, 1, "unrelated"});
    store.append({protocol::MessageType::DirectChat, 2, 1, "two to one"});
    store.append({protocol::MessageType::GroupChat, 1, 2, "wrong type"});

    const auto history = store.direct_history(1, 2);
    ASSERT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0].sender_id, 1u);
    EXPECT_EQ(history[0].receiver_id, 2u);
    EXPECT_EQ(history[0].body, "one to two");
    EXPECT_EQ(history[1].sender_id, 2u);
    EXPECT_EQ(history[1].receiver_id, 1u);
    EXPECT_EQ(history[1].body, "two to one");
}

TEST(MessageStore, GroupHistoryReturnsOnlyGroupMessages) {
    MessageStore store;
    store.append({protocol::MessageType::GroupChat, 1, 100, "first"});
    store.append({protocol::MessageType::GroupChat, 2, 200, "other group"});
    store.append({protocol::MessageType::DirectChat, 1, 100, "wrong type"});
    store.append({protocol::MessageType::GroupChat, 3, 100, "second"});

    const auto history = store.group_history(100);
    ASSERT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0].sender_id, 1u);
    EXPECT_EQ(history[0].receiver_id, 100u);
    EXPECT_EQ(history[0].body, "first");
    EXPECT_EQ(history[1].sender_id, 3u);
    EXPECT_EQ(history[1].receiver_id, 100u);
    EXPECT_EQ(history[1].body, "second");
}

TEST(MessageStore, DirectHistoryPageUsesMessageIdCursorNewestFirst) {
    MessageStore store;
    store.append({protocol::MessageType::DirectChat, 1, 2, "one"});
    store.append({protocol::MessageType::DirectChat, 2, 1, "two"});
    store.append({protocol::MessageType::DirectChat, 3, 1, "unrelated"});
    store.append({protocol::MessageType::DirectChat, 1, 2, "three"});

    const auto first_page = store.direct_history_page(1, 2, 2, 0);
    ASSERT_EQ(first_page.size(), 2u);
    EXPECT_EQ(first_page[0].id, 4u);
    EXPECT_EQ(first_page[0].body, "three");
    EXPECT_EQ(first_page[1].id, 2u);
    EXPECT_EQ(first_page[1].body, "two");

    const auto second_page = store.direct_history_page(1, 2, 2, first_page[1].id);
    ASSERT_EQ(second_page.size(), 1u);
    EXPECT_EQ(second_page[0].id, 1u);
    EXPECT_EQ(second_page[0].body, "one");
}

TEST(MessageStore, GroupHistoryPageUsesMessageIdCursorNewestFirst) {
    MessageStore store;
    store.append({protocol::MessageType::GroupChat, 1, 100, "one"});
    store.append({protocol::MessageType::GroupChat, 2, 100, "two"});
    store.append({protocol::MessageType::GroupChat, 3, 200, "other"});
    store.append({protocol::MessageType::GroupChat, 1, 100, "three"});

    const auto first_page = store.group_history_page(100, 2, 0);
    ASSERT_EQ(first_page.size(), 2u);
    EXPECT_EQ(first_page[0].id, 4u);
    EXPECT_EQ(first_page[0].body, "three");
    EXPECT_EQ(first_page[1].id, 2u);
    EXPECT_EQ(first_page[1].body, "two");

    const auto second_page = store.group_history_page(100, 2, first_page[1].id);
    ASSERT_EQ(second_page.size(), 1u);
    EXPECT_EQ(second_page[0].id, 1u);
    EXPECT_EQ(second_page[0].body, "one");
}

TEST(MessageStore, UnreadPageReturnsDirectAndJoinedGroupMessagesAfterCursor) {
    MessageStore store;
    store.append({protocol::MessageType::DirectChat, 1, 2, "dm old"});
    store.append({protocol::MessageType::DirectChat, 1, 2, "dm new"});
    store.append({protocol::MessageType::DirectChat, 2, 1, "sent by me"});
    store.append({protocol::MessageType::GroupChat, 3, 100, "group new"});
    store.append({protocol::MessageType::GroupChat, 2, 100, "own group"});
    store.append({protocol::MessageType::GroupChat, 4, 200, "other group"});

    const auto unread = store.unread_page(2, {100}, 1, 10);

    ASSERT_EQ(unread.size(), 2u);
    EXPECT_EQ(unread[0].id, 2u);
    EXPECT_EQ(unread[0].body, "dm new");
    EXPECT_EQ(unread[1].id, 4u);
    EXPECT_EQ(unread[1].body, "group new");
}

TEST(MessageStore, UnreadPageRespectsLimit) {
    MessageStore store;
    store.append({protocol::MessageType::DirectChat, 1, 2, "one"});
    store.append({protocol::MessageType::DirectChat, 1, 2, "two"});

    const auto unread = store.unread_page(2, {}, 0, 1);

    ASSERT_EQ(unread.size(), 1u);
    EXPECT_EQ(unread[0].body, "one");
}

} // namespace cpp_chat::storage
