#include "cpp_chat/storage/group_member_store.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <mysql.h>

namespace cpp_chat::storage {

namespace {

bool execute_query(MYSQL* mysql, const char* query) {
    return mysql_query(mysql, query) == 0;
}

bool bind_uint64_params(MYSQL_STMT* statement,
                        MYSQL_BIND* params,
                        std::uint64_t& first,
                        std::uint64_t& second) {
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &first;
    params[0].is_unsigned = true;

    params[1].buffer_type = MYSQL_TYPE_LONGLONG;
    params[1].buffer = &second;
    params[1].is_unsigned = true;

    return mysql_stmt_bind_param(statement, params) == 0;
}

} // namespace

GroupMemberStore::GroupMemberStore(MySqlConnectionPool& pool) : pool_(&pool) {
    auto connection = pool_->acquire();
    if (!connection) {
        std::cerr << "[ERROR] GroupMemberStore: failed to acquire MySQL connection" << std::endl;
        return;
    }

    MYSQL* mysql = connection.get();
    const char* create_table =
        "CREATE TABLE IF NOT EXISTS group_members ("
        "group_id BIGINT UNSIGNED NOT NULL,"
        "user_id BIGINT UNSIGNED NOT NULL,"
        "joined_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY (group_id, user_id),"
        "INDEX idx_group_members_user_group (user_id, group_id),"
        "INDEX idx_group_members_joined_at (joined_at),"
        "CONSTRAINT fk_group_members_user "
        "FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (!execute_query(mysql, create_table)) {
        std::cerr << "[ERROR] GroupMemberStore: failed to create group_members table: "
                  << mysql_error(mysql) << std::endl;
    }
}

GroupMemberResult GroupMemberStore::join_group(std::uint64_t group_id, std::uint64_t user_id) {
    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto [_, inserted] = memory_members_[group_id].insert(user_id);
        return {true, !inserted, {}};
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return {false, false, "database error"};
    }

    MYSQL* mysql = connection.get();
    const char* query = "INSERT IGNORE INTO group_members (group_id, user_id) VALUES (?, ?)";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return {false, false, "database error"};
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to prepare join_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, false, "database error"};
    }

    MYSQL_BIND params[2]{};
    if (!bind_uint64_params(statement, params, group_id, user_id)) {
        std::cerr << "[ERROR] GroupMemberStore: failed to bind join_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, false, "database error"};
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to execute join_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, false, "database error"};
    }

    const auto affected_rows = mysql_stmt_affected_rows(statement);
    mysql_stmt_close(statement);
    return {true, affected_rows == 0, {}};
}

GroupMemberResult GroupMemberStore::leave_group(std::uint64_t group_id, std::uint64_t user_id) {
    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto group_it = memory_members_.find(group_id);
        if (group_it == memory_members_.end() || group_it->second.erase(user_id) == 0) {
            return {false, false, "not a member of this group"};
        }
        if (group_it->second.empty()) {
            memory_members_.erase(group_it);
        }
        return {true, false, {}};
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return {false, false, "database error"};
    }

    MYSQL* mysql = connection.get();
    const char* query = "DELETE FROM group_members WHERE group_id = ? AND user_id = ?";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return {false, false, "database error"};
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to prepare leave_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, false, "database error"};
    }

    MYSQL_BIND params[2]{};
    if (!bind_uint64_params(statement, params, group_id, user_id)) {
        std::cerr << "[ERROR] GroupMemberStore: failed to bind leave_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, false, "database error"};
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to execute leave_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, false, "database error"};
    }

    const auto affected_rows = mysql_stmt_affected_rows(statement);
    mysql_stmt_close(statement);
    if (affected_rows == 0) {
        return {false, false, "not a member of this group"};
    }
    return {true, false, {}};
}

bool GroupMemberStore::is_group_member(std::uint64_t group_id, std::uint64_t user_id) const {
    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto group_it = memory_members_.find(group_id);
        return group_it != memory_members_.end() &&
               group_it->second.find(user_id) != group_it->second.end();
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return false;
    }

    MYSQL* mysql = connection.get();
    const char* query = "SELECT 1 FROM group_members WHERE group_id = ? AND user_id = ? LIMIT 1";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return false;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to prepare is_group_member: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return false;
    }

    MYSQL_BIND params[2]{};
    if (!bind_uint64_params(statement, params, group_id, user_id)) {
        std::cerr << "[ERROR] GroupMemberStore: failed to bind is_group_member: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return false;
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to execute is_group_member: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return false;
    }

    MYSQL_BIND result[1]{};
    int found = 0;
    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &found;

    if (mysql_stmt_bind_result(statement, result) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to bind is_group_member result: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return false;
    }

    const bool exists = mysql_stmt_fetch(statement) == 0;
    mysql_stmt_close(statement);
    return exists;
}

std::vector<std::uint64_t> GroupMemberStore::load_group_members(std::uint64_t group_id) const {
    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::uint64_t> members;
        const auto group_it = memory_members_.find(group_id);
        if (group_it == memory_members_.end()) {
            return members;
        }
        members.assign(group_it->second.begin(), group_it->second.end());
        std::sort(members.begin(), members.end());
        return members;
    }

    std::vector<std::uint64_t> members;
    auto connection = pool_->acquire();
    if (!connection) {
        return members;
    }

    MYSQL* mysql = connection.get();
    const char* query = "SELECT user_id FROM group_members WHERE group_id = ? ORDER BY user_id ASC";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return members;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to prepare load_group_members: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return members;
    }

    MYSQL_BIND params[1]{};
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &group_id;
    params[0].is_unsigned = true;

    if (mysql_stmt_bind_param(statement, params) != 0 ||
        mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to execute load_group_members: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return members;
    }

    MYSQL_BIND result[1]{};
    std::uint64_t user_id = 0;
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &user_id;
    result[0].is_unsigned = true;

    if (mysql_stmt_bind_result(statement, result) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to bind load_group_members result: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return members;
    }

    while (true) {
        const int status = mysql_stmt_fetch(statement);
        if (status == MYSQL_NO_DATA) {
            break;
        }
        if (status != 0 && status != MYSQL_DATA_TRUNCATED) {
            std::cerr << "[ERROR] GroupMemberStore: failed to fetch load_group_members row: "
                      << mysql_stmt_error(statement) << std::endl;
            break;
        }
        members.push_back(user_id);
    }

    mysql_stmt_close(statement);
    return members;
}

std::vector<std::uint64_t> GroupMemberStore::load_user_groups(std::uint64_t user_id) const {
    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::uint64_t> groups;
        for (const auto& [group_id, members] : memory_members_) {
            if (members.find(user_id) != members.end()) {
                groups.push_back(group_id);
            }
        }
        std::sort(groups.begin(), groups.end());
        return groups;
    }

    std::vector<std::uint64_t> groups;
    auto connection = pool_->acquire();
    if (!connection) {
        return groups;
    }

    MYSQL* mysql = connection.get();
    const char* query = "SELECT group_id FROM group_members WHERE user_id = ? ORDER BY group_id ASC";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return groups;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to prepare load_user_groups: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return groups;
    }

    MYSQL_BIND params[1]{};
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &user_id;
    params[0].is_unsigned = true;

    if (mysql_stmt_bind_param(statement, params) != 0 ||
        mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to execute load_user_groups: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return groups;
    }

    MYSQL_BIND result[1]{};
    std::uint64_t group_id = 0;
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &group_id;
    result[0].is_unsigned = true;

    if (mysql_stmt_bind_result(statement, result) != 0) {
        std::cerr << "[ERROR] GroupMemberStore: failed to bind load_user_groups result: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return groups;
    }

    while (true) {
        const int status = mysql_stmt_fetch(statement);
        if (status == MYSQL_NO_DATA) {
            break;
        }
        if (status != 0 && status != MYSQL_DATA_TRUNCATED) {
            std::cerr << "[ERROR] GroupMemberStore: failed to fetch load_user_groups row: "
                      << mysql_stmt_error(statement) << std::endl;
            break;
        }
        groups.push_back(group_id);
    }

    mysql_stmt_close(statement);
    return groups;
}

} // namespace cpp_chat::storage
