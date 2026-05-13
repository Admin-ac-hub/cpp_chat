#include "cpp_chat/storage/user_store.h"

#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mysql.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <utility>
#include <vector>

namespace cpp_chat::storage {

namespace {

constexpr int kPbkdf2Iterations = 100000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

bool execute_query(MYSQL* mysql, const char* query) {
    return mysql_query(mysql, query) == 0;
}

std::string to_hex(const unsigned char* data, std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        out << std::setw(2) << static_cast<int>(data[i]);
    }
    return out.str();
}

std::vector<unsigned char> from_hex(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return {};
    }
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto byte = hex.substr(i, 2);
        char* end = nullptr;
        const long value = std::strtol(byte.c_str(), &end, 16);
        if (end == nullptr || *end != '\0' || value < 0 || value > 255) {
            return {};
        }
        bytes.push_back(static_cast<unsigned char>(value));
    }
    return bytes;
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    std::istringstream input(value);
    while (std::getline(input, current, delimiter)) {
        parts.push_back(current);
    }
    return parts;
}

std::string make_hash(const std::string& password,
                      int iterations,
                      const std::vector<unsigned char>& salt) {
    unsigned char hash[kHashBytes] = {};
    if (PKCS5_PBKDF2_HMAC(password.c_str(),
                          static_cast<int>(password.size()),
                          salt.data(),
                          static_cast<int>(salt.size()),
                          iterations,
                          EVP_sha256(),
                          static_cast<int>(sizeof(hash)),
                          hash) != 1) {
        return {};
    }

    return "pbkdf2_sha256$" + std::to_string(iterations) + "$" +
           to_hex(salt.data(), salt.size()) + "$" + to_hex(hash, sizeof(hash));
}

std::optional<UserRecord> fetch_user_by_username(MYSQL* mysql, const std::string& username) {
    const char* query = "SELECT id, username, password_hash FROM users WHERE username = ?";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return std::nullopt;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    MYSQL_BIND param[1]{};
    unsigned long username_length = static_cast<unsigned long>(username.size());
    param[0].buffer_type = MYSQL_TYPE_STRING;
    param[0].buffer = const_cast<char*>(username.data());
    param[0].buffer_length = username_length;
    param[0].length = &username_length;

    if (mysql_stmt_bind_param(statement, param) != 0 || mysql_stmt_execute(statement) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    MYSQL_BIND result[3]{};
    std::uint64_t id = 0;
    char username_buffer[65] = {};
    char hash_buffer[256] = {};
    unsigned long fetched_username_length = 0;
    unsigned long fetched_hash_length = 0;

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[0].is_unsigned = true;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = username_buffer;
    result[1].buffer_length = sizeof(username_buffer);
    result[1].length = &fetched_username_length;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = hash_buffer;
    result[2].buffer_length = sizeof(hash_buffer);
    result[2].length = &fetched_hash_length;

    if (mysql_stmt_bind_result(statement, result) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    const int status = mysql_stmt_fetch(statement);
    if (status == MYSQL_NO_DATA) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }
    if (status != 0 && status != MYSQL_DATA_TRUNCATED) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    UserRecord user;
    user.id = id;
    user.username.assign(username_buffer, fetched_username_length);
    user.password_hash.assign(hash_buffer, fetched_hash_length);
    mysql_stmt_close(statement);
    return user;
}

std::optional<UserRecord> fetch_user_by_id(MYSQL* mysql, std::uint64_t user_id) {
    const char* query = "SELECT id, username, password_hash FROM users WHERE id = ?";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return std::nullopt;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    MYSQL_BIND param[1]{};
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &user_id;
    param[0].is_unsigned = true;

    if (mysql_stmt_bind_param(statement, param) != 0 || mysql_stmt_execute(statement) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    MYSQL_BIND result[3]{};
    std::uint64_t id = 0;
    char username_buffer[65] = {};
    char hash_buffer[256] = {};
    unsigned long username_length = 0;
    unsigned long hash_length = 0;

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[0].is_unsigned = true;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = username_buffer;
    result[1].buffer_length = sizeof(username_buffer);
    result[1].length = &username_length;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = hash_buffer;
    result[2].buffer_length = sizeof(hash_buffer);
    result[2].length = &hash_length;

    if (mysql_stmt_bind_result(statement, result) != 0) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    const int status = mysql_stmt_fetch(statement);
    if (status == MYSQL_NO_DATA) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }
    if (status != 0 && status != MYSQL_DATA_TRUNCATED) {
        mysql_stmt_close(statement);
        return std::nullopt;
    }

    UserRecord user;
    user.id = id;
    user.username.assign(username_buffer, username_length);
    user.password_hash.assign(hash_buffer, hash_length);
    mysql_stmt_close(statement);
    return user;
}

} // namespace

std::string hash_password_pbkdf2(const std::string& password) {
    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        return {};
    }
    return make_hash(password, kPbkdf2Iterations, salt);
}

bool verify_password_pbkdf2(const std::string& password, const std::string& stored_hash) {
    const auto parts = split(stored_hash, '$');
    if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") {
        return false;
    }

    int iterations = 0;
    try {
        iterations = std::stoi(parts[1]);
    } catch (...) {
        return false;
    }
    if (iterations <= 0) {
        return false;
    }

    const auto salt = from_hex(parts[2]);
    if (salt.empty()) {
        return false;
    }
    const auto recalculated = make_hash(password, iterations, salt);
    return !recalculated.empty() && recalculated == stored_hash;
}

UserStore::UserStore(MySqlConnectionPool& pool) : pool_(&pool) {
    auto connection = pool_->acquire();
    if (!connection) {
        std::cerr << "[ERROR] UserStore: failed to acquire MySQL connection" << std::endl;
        return;
    }
    MYSQL* mysql = connection.get();

    const char* create_table =
        "CREATE TABLE IF NOT EXISTS users ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "username VARCHAR(64) NOT NULL UNIQUE,"
        "password_hash VARCHAR(255) NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (!execute_query(mysql, create_table)) {
        std::cerr << "[ERROR] UserStore: failed to create users table: "
                  << mysql_error(mysql) << std::endl;
    }
}

RegisterResult UserStore::register_user(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {
        return {false, 0, "username and password are required"};
    }
    if (username.size() > 64) {
        return {false, 0, "username too long"};
    }

    if (pool_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (users_by_username_.find(username) != users_by_username_.end()) {
            return {false, 0, "username already exists"};
        }
        const auto password_hash = hash_password_pbkdf2(password);
        if (password_hash.empty()) {
            return {false, 0, "failed to hash password"};
        }
        const auto user_id = next_memory_id_++;
        UserRecord user{user_id, username, password_hash};
        users_by_username_[username] = user;
        usernames_by_id_[user_id] = username;
        return {true, user_id, {}};
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return {false, 0, "database error"};
    }
    MYSQL* mysql = connection.get();

    if (fetch_user_by_username(mysql, username).has_value()) {
        return {false, 0, "username already exists"};
    }

    const auto password_hash = hash_password_pbkdf2(password);
    if (password_hash.empty()) {
        return {false, 0, "failed to hash password"};
    }

    const char* query = "INSERT INTO users (username, password_hash) VALUES (?, ?)";
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return {false, 0, "database error"};
    }
    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        mysql_stmt_close(statement);
        return {false, 0, "database error"};
    }

    MYSQL_BIND params[2]{};
    unsigned long username_length = static_cast<unsigned long>(username.size());
    unsigned long hash_length = static_cast<unsigned long>(password_hash.size());
    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = const_cast<char*>(username.data());
    params[0].buffer_length = username_length;
    params[0].length = &username_length;
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = const_cast<char*>(password_hash.data());
    params[1].buffer_length = hash_length;
    params[1].length = &hash_length;

    if (mysql_stmt_bind_param(statement, params) != 0 || mysql_stmt_execute(statement) != 0) {
        mysql_stmt_close(statement);
        if (mysql_errno(mysql) == 1062) {
            return {false, 0, "username already exists"};
        }
        return {false, 0, "database error"};
    }

    const auto user_id = static_cast<std::uint64_t>(mysql_insert_id(mysql));
    mysql_stmt_close(statement);
    return {true, user_id, {}};
}

std::optional<UserRecord> UserStore::find_user_by_username(const std::string& username) const {
    if (pool_ != nullptr) {
        auto connection = pool_->acquire();
        if (!connection) {
            return std::nullopt;
        }
        return fetch_user_by_username(connection.get(), username);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = users_by_username_.find(username);
    if (it == users_by_username_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<UserRecord> UserStore::find_user_by_id(std::uint64_t user_id) const {
    if (pool_ != nullptr) {
        auto connection = pool_->acquire();
        if (!connection) {
            return std::nullopt;
        }
        return fetch_user_by_id(connection.get(), user_id);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto username_it = usernames_by_id_.find(user_id);
    if (username_it == usernames_by_id_.end()) {
        return std::nullopt;
    }
    const auto user_it = users_by_username_.find(username_it->second);
    if (user_it == users_by_username_.end()) {
        return std::nullopt;
    }
    return user_it->second;
}

bool UserStore::verify_login(const std::string& username,
                             const std::string& password,
                             UserRecord& out_user) const {
    const auto user = find_user_by_username(username);
    if (!user.has_value()) {
        return false;
    }
    if (!verify_password_pbkdf2(password, user->password_hash)) {
        return false;
    }
    out_user = *user;
    return true;
}

} // namespace cpp_chat::storage
