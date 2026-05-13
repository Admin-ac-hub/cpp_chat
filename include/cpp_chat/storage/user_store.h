#pragma once

#include "cpp_chat/storage/mysql_connection_pool.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace cpp_chat::storage {

struct UserRecord {
    std::uint64_t id = 0;
    std::string username;
    std::string password_hash;
};

struct RegisterResult {
    bool success = false;
    std::uint64_t user_id = 0;
    std::string reason;
};

class UserStore {
public:
    UserStore() = default;
    explicit UserStore(MySqlConnectionPool& pool);
    ~UserStore() = default;

    UserStore(const UserStore&) = delete;
    UserStore& operator=(const UserStore&) = delete;

    RegisterResult register_user(const std::string& username, const std::string& password);
    std::optional<UserRecord> find_user_by_username(const std::string& username) const;
    std::optional<UserRecord> find_user_by_id(std::uint64_t user_id) const;
    bool verify_login(const std::string& username, const std::string& password, UserRecord& out_user) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, UserRecord> users_by_username_;
    std::unordered_map<std::uint64_t, std::string> usernames_by_id_;
    std::uint64_t next_memory_id_ = 1;

    MySqlConnectionPool* pool_ = nullptr;
};

std::string hash_password_pbkdf2(const std::string& password);
bool verify_password_pbkdf2(const std::string& password, const std::string& stored_hash);

} // namespace cpp_chat::storage
