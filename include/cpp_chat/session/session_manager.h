#pragma once

#include "cpp_chat/session/session.h"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cpp_chat::session {

// 群组 ID，与用户 ID 同为 uint64_t。
using GroupId = std::uint64_t;

// 在线会话管理器。
//
// 内部维护两张索引：
// - user_id -> Session：按用户查找连接。
// - connection_id -> user_id：按连接反查登录用户。
//
// 另维护群组成员关系（纯内存，不持久化）。
// 所有公开方法均已加锁，支持网络线程和工作线程并发访问。
class SessionManager {
public:
    // 绑定用户和连接。
    // 如果同一用户或同一连接已经存在绑定，会先移除旧关系，保证一对一。
    void bind(Session session);

    // 按用户 ID 下线。
    void unbind(UserId user_id);

    // 按连接 ID 下线，主要用于 socket 断开后的清理。
    void unbind_connection(network::ConnectionId connection_id);

    // 查找某个在线用户的会话；不存在时返回 nullopt。
    std::optional<Session> find(UserId user_id) const;

    // 根据连接反查当前登录用户；未登录连接返回 nullopt。
    std::optional<Session> find_by_connection(network::ConnectionId connection_id) const;

    // 根据用户名查在线会话。
    std::optional<Session> find_by_username(const std::string& username) const;

    // 判断用户名当前是否在线。
    bool is_username_online(const std::string& username) const;

    // ── 群组操作 ──

    // 将用户加入群组。同一用户重复加入同一群组无副作用。
    void join_group(GroupId group_id, UserId user_id);

    // 将用户移出群组。用户不在群组中时无副作用。
    void leave_group(GroupId group_id, UserId user_id);

    // 获取群组所有成员的用户 ID 列表（副本）。
    std::vector<UserId> get_group_members(GroupId group_id) const;

    // 判断用户是否在群组中。
    bool is_group_member(GroupId group_id, UserId user_id) const;

private:
    // 保护两张索引的并发访问。
    mutable std::mutex mutex_;

    // 用户到会话的主索引。
    std::unordered_map<UserId, Session> sessions_;

    // 连接到用户的反向索引，用于断连清理和识别发送者。
    std::unordered_map<network::ConnectionId, UserId> users_by_connection_;

    // 用户名到用户 ID 的索引，用于拒绝重复登录和按用户名投递。
    std::unordered_map<std::string, UserId> users_by_username_;

    // 群组成员：group_id -> 成员 user_id 集合。
    std::unordered_map<GroupId, std::unordered_set<UserId>> group_members_;
};

} // namespace cpp_chat::session
