#pragma once

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
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
    int connect_timeout_seconds = 3;
    int read_timeout_seconds = 5;
    int write_timeout_seconds = 5;
    int acquire_timeout_ms = 3000;
    int max_reconnect_attempts = 3;
    int idle_ping_interval_seconds = 30;
    int idle_check_interval_seconds = 10;
};

class MySqlConnectionPool;

struct PooledConnection {
    MYSQL* mysql = nullptr;
    std::chrono::steady_clock::time_point last_used{};
    std::chrono::steady_clock::time_point last_checked{};
    std::uint64_t generation = 0;
};

class PooledMySqlConnection {
public:
    PooledMySqlConnection() = default;
    PooledMySqlConnection(MySqlConnectionPool* pool, PooledConnection* connection);
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
    PooledConnection* connection_ = nullptr;
};

using ConnectionGuard = PooledMySqlConnection;

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

    MYSQL* create_connection();
    void close_connection(PooledConnection& connection);
    bool ping_connection(PooledConnection& connection);
    bool reconnect_connection(PooledConnection& connection);
    bool ensure_healthy(PooledConnection& connection);
    void release(PooledConnection* connection);
    void close_all_unlocked();
    std::vector<PooledConnection*> idle_health_check_unlocked();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PooledConnection*> available_;
    std::vector<std::unique_ptr<PooledConnection>> connections_;
    MySqlConnectionPoolConfig config_;
    std::chrono::steady_clock::time_point last_idle_check_{};
    std::atomic_uint64_t next_generation_{1};
    bool ready_ = false;
    bool stopped_ = true;
};

} // namespace cpp_chat::storage
