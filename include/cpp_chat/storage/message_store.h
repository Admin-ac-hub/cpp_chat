#pragma once

#include "cpp_chat/protocol/message.h"
#include "cpp_chat/storage/mysql_connection_pool.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cpp_chat::storage {

// 消息存储。
//
// 默认构造使用内存 vector，适合测试或不需要持久化的场景；
// 传入连接池后，append() 会同时将消息写入 MySQL messages 表。
// 接口不变，ChatService 无需感知底层存储实现。
class MessageStore {
public:
    // 内存模式：进程重启后消息丢失，适合测试。
    MessageStore() = default;

    // MySQL 模式：使用共享连接池并确保 messages 表存在，append() 会持久化消息。
    explicit MessageStore(MySqlConnectionPool& pool);

    ~MessageStore() = default;

    // 存储类持有连接池引用，禁止拷贝，避免悬空引用和并发语义不清。
    MessageStore(const MessageStore&) = delete;
    MessageStore& operator=(const MessageStore&) = delete;

    // 追加一条消息：写入内存，如果 MySQL 可用则同时持久化。
    void append(protocol::Message message);

    // 返回内存中所有消息的副本。
    // 注意：服务重启后内存为空，历史消息仅在 MySQL 中保留。
    std::vector<protocol::Message> all() const;

    // 查询两个用户之间的私聊历史。
    //
    // MySQL 可用时直接从 messages 表读取；不可用时从内存副本筛选。
    std::vector<protocol::Message> direct_history(std::uint64_t user_id,
                                                  std::uint64_t peer_id) const;

    // 查询某个群组的群聊历史。
    //
    // MySQL 可用时直接从 messages 表读取；不可用时从内存副本筛选。
    std::vector<protocol::Message> group_history(std::uint64_t group_id) const;

private:
    // 保护内存容器。
    // mutable 允许在 const 方法（all()）中加锁，这是互斥量的惯用写法。
    mutable std::mutex mutex_;

    // 按接收顺序在内存中保存消息，供当次运行期间快速检索。
    std::vector<protocol::Message> messages_;

    // 共享 MySQL 连接池；nullptr 表示仅使用内存模式。
    MySqlConnectionPool* pool_ = nullptr;
};

} // namespace cpp_chat::storage
