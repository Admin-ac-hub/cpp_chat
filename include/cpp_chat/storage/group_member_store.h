#pragma once

#include "cpp_chat/storage/mysql_connection_pool.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cpp_chat::storage {

struct GroupMemberResult {
    bool success = false;
    bool already_member = false;
    std::string reason;
};

// 群成员关系存储。
//
// 默认构造使用内存模式，适合快速单元测试；传入 MySQL 连接池后，
// group_members 表成为群成员关系的权威数据源，服务重启后仍可恢复。
class GroupMemberStore {
public:
    GroupMemberStore() = default;
    explicit GroupMemberStore(MySqlConnectionPool& pool);
    ~GroupMemberStore() = default;

    GroupMemberStore(const GroupMemberStore&) = delete;
    GroupMemberStore& operator=(const GroupMemberStore&) = delete;

    GroupMemberResult join_group(std::uint64_t group_id, std::uint64_t user_id);
    GroupMemberResult leave_group(std::uint64_t group_id, std::uint64_t user_id);

    bool is_group_member(std::uint64_t group_id, std::uint64_t user_id) const;
    std::vector<std::uint64_t> load_group_members(std::uint64_t group_id) const;
    std::vector<std::uint64_t> load_user_groups(std::uint64_t user_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> memory_members_;

    MySqlConnectionPool* pool_ = nullptr;
};

} // namespace cpp_chat::storage
