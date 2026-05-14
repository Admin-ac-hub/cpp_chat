#pragma once

#include "cpp_chat/storage/mysql_connection_pool.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace cpp_chat::storage {

struct GroupRecord {
    std::uint64_t id = 0;
    std::string name;
    std::uint64_t owner_id = 0;
    std::string created_at;
};

struct CreateGroupResult {
    bool success = false;
    GroupRecord group;
    std::string reason;
};

class GroupStore {
public:
    GroupStore() = default;
    explicit GroupStore(MySqlConnectionPool& pool);
    ~GroupStore() = default;

    GroupStore(const GroupStore&) = delete;
    GroupStore& operator=(const GroupStore&) = delete;

    CreateGroupResult create_group(const std::string& name, std::uint64_t owner_id);
    std::optional<GroupRecord> find_group(std::uint64_t group_id) const;
    bool group_exists(std::uint64_t group_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, GroupRecord> memory_groups_;
    std::uint64_t next_group_id_ = 1;

    MySqlConnectionPool* pool_ = nullptr;
};

} // namespace cpp_chat::storage
