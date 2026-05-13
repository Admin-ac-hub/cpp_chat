#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

struct MYSQL;

namespace cpp_chat::storage {

struct MySqlConnectionPoolConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string user = "cpp_chat";
    std::string password = "cpp_chat";
    std::string database = "cpp_chat";
    int pool_size = 4;
};

class MySqlConnectionPool;

class PooledMySqlConnection {
public:
    PooledMySqlConnection() = default;
    PooledMySqlConnection(MySqlConnectionPool* pool, MYSQL* connection);
    ~PooledMySqlConnection();

    PooledMySqlConnection(const PooledMySqlConnection&) = delete;
    PooledMySqlConnection& operator=(const PooledMySqlConnection&) = delete;

    PooledMySqlConnection(PooledMySqlConnection&& other) noexcept;
    PooledMySqlConnection& operator=(PooledMySqlConnection&& other) noexcept;

    MYSQL* get() const;
    explicit operator bool() const;

private:
    void reset();

    MySqlConnectionPool* pool_ = nullptr;
    MYSQL* connection_ = nullptr;
};

class MySqlConnectionPool {
public:
    MySqlConnectionPool() = default;
    explicit MySqlConnectionPool(const MySqlConnectionPoolConfig& config);
    ~MySqlConnectionPool();

    MySqlConnectionPool(const MySqlConnectionPool&) = delete;
    MySqlConnectionPool& operator=(const MySqlConnectionPool&) = delete;

    bool init(const MySqlConnectionPoolConfig& config);
    PooledMySqlConnection acquire();
    void shutdown();
    bool ready() const;

private:
    friend class PooledMySqlConnection;

    void release(MYSQL* connection);
    void close_all_unlocked();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<MYSQL*> available_;
    std::vector<MYSQL*> all_connections_;
    MySqlConnectionPoolConfig config_;
    bool ready_ = false;
    bool stopped_ = true;
};

} // namespace cpp_chat::storage
