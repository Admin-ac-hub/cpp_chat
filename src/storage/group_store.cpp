#include "cpp_chat/storage/group_store.h"

#include <cstring>
#include <iostream>
#include <mysql.h>

namespace cpp_chat::storage {

namespace {

bool execute_query(MYSQL* mysql, const char* query) {
    return mysql_query(mysql, query) == 0;
}

} // namespace

GroupStore::GroupStore(MySqlConnectionPool& pool) : pool_(&pool) {
    auto connection = pool_->acquire();
    if (!connection) {
        std::cerr << "[ERROR] GroupStore: failed to acquire MySQL connection" << std::endl;
        return;
    }

    MYSQL* mysql = connection.get();
    const char* create_table =
        "CREATE TABLE IF NOT EXISTS `groups` ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "name VARCHAR(128) NOT NULL,"
        "owner_id BIGINT UNSIGNED NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "INDEX idx_groups_owner (owner_id),"
        "CONSTRAINT fk_groups_owner "
        "FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (!execute_query(mysql, create_table)) {
        std::cerr << "[ERROR] GroupStore: failed to create groups table: "
                  << mysql_error(mysql) << std::endl;
    }
}

CreateGroupResult GroupStore::create_group(const std::string& name, std::uint64_t owner_id) {
    if (name.empty()) {
        return {false, {}, "group name is required"};
    }

    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        GroupRecord group;
        group.id = next_group_id_++;
        group.name = name;
        group.owner_id = owner_id;
        memory_groups_[group.id] = group;
        return {true, group, {}};
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return {false, {}, "database error"};
    }

    MYSQL* mysql = connection.get();
    const char* query = "INSERT INTO `groups` (name, owner_id) VALUES (?, ?)";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return {false, {}, "database error"};
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] GroupStore: failed to prepare create_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, {}, "database error"};
    }

    unsigned long name_length = static_cast<unsigned long>(name.size());
    MYSQL_BIND params[2]{};
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = const_cast<char*>(name.data());
    params[0].buffer_length = name_length;
    params[0].length = &name_length;

    params[1].buffer_type = MYSQL_TYPE_LONGLONG;
    params[1].buffer = &owner_id;
    params[1].is_unsigned = true;

    if (mysql_stmt_bind_param(statement, params) != 0 ||
        mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] GroupStore: failed to execute create_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {false, {}, "database error"};
    }

    GroupRecord group;
    group.id = mysql_insert_id(mysql);
    group.name = name;
    group.owner_id = owner_id;
    mysql_stmt_close(statement);
    return {true, group, {}};
}

std::optional<GroupRecord> GroupStore::find_group(std::uint64_t group_id) const {
    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = memory_groups_.find(group_id);
        if (it == memory_groups_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return std::nullopt;
    }

    MYSQL* mysql = connection.get();
    const char* query =
        "SELECT id, name, owner_id, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM `groups` WHERE id = ? LIMIT 1";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return std::nullopt;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] GroupStore: failed to prepare find_group: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    MYSQL_BIND params[1]{};
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &group_id;
    params[0].is_unsigned = true;
    if (mysql_stmt_bind_param(statement, params) != 0 ||
        mysql_stmt_execute(statement) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    GroupRecord group;
    char name[129]{};
    unsigned long name_length = 0;
    char created_at[32]{};
    unsigned long created_at_length = 0;
    MYSQL_BIND result[4]{};
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &group.id;
    result[0].is_unsigned = true;

    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = name;
    result[1].buffer_length = sizeof(name);
    result[1].length = &name_length;

    result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result[2].buffer = &group.owner_id;
    result[2].is_unsigned = true;

    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = created_at;
    result[3].buffer_length = sizeof(created_at);
    result[3].length = &created_at_length;

    if (mysql_stmt_bind_result(statement, result) != 0 ||
        mysql_stmt_fetch(statement) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    group.name.assign(name, name_length);
    group.created_at.assign(created_at, created_at_length);
    mysql_stmt_close(statement);
    return group;
}

bool GroupStore::group_exists(std::uint64_t group_id) const {
    return find_group(group_id).has_value();
}

} // namespace cpp_chat::storage
