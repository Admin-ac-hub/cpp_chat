#include "cpp_chat/storage/mysql_connection_pool.h"

#include <iostream>
#include <mysql.h>
#include <utility>

namespace cpp_chat::storage {

PooledMySqlConnection::PooledMySqlConnection(MySqlConnectionPool* pool, MYSQL* connection)
    : pool_(pool), connection_(connection) {}

PooledMySqlConnection::~PooledMySqlConnection() {
    reset();
}

PooledMySqlConnection::PooledMySqlConnection(PooledMySqlConnection&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      connection_(std::exchange(other.connection_, nullptr)) {}

PooledMySqlConnection& PooledMySqlConnection::operator=(PooledMySqlConnection&& other) noexcept {
    if (this != &other) {
        reset();
        pool_ = std::exchange(other.pool_, nullptr);
        connection_ = std::exchange(other.connection_, nullptr);
    }
    return *this;
}

MYSQL* PooledMySqlConnection::get() const {
    return connection_;
}

PooledMySqlConnection::operator bool() const {
    return connection_ != nullptr;
}

void PooledMySqlConnection::reset() {
    if (pool_ != nullptr && connection_ != nullptr) {
        pool_->release(connection_);
    }
    pool_ = nullptr;
    connection_ = nullptr;
}

MySqlConnectionPool::MySqlConnectionPool(const MySqlConnectionPoolConfig& config) {
    init(config);
}

MySqlConnectionPool::~MySqlConnectionPool() {
    shutdown();
}

bool MySqlConnectionPool::init(const MySqlConnectionPoolConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ready_) {
        return true;
    }

    config_ = config;
    stopped_ = false;
    ready_ = false;
    close_all_unlocked();

    if (config_.pool_size <= 0) {
        std::cerr << "[ERROR] MySqlConnectionPool: pool_size must be positive" << std::endl;
        stopped_ = true;
        return false;
    }

    std::vector<MYSQL*> created;
    created.reserve(static_cast<std::size_t>(config_.pool_size));
    for (int i = 0; i < config_.pool_size; ++i) {
        MYSQL* mysql = mysql_init(nullptr);
        if (mysql == nullptr) {
            std::cerr << "[ERROR] MySqlConnectionPool: mysql_init failed" << std::endl;
            for (MYSQL* connection : created) {
                mysql_close(connection);
            }
            stopped_ = true;
            return false;
        }

        if (mysql_real_connect(mysql,
                               config_.host.c_str(),
                               config_.user.c_str(),
                               config_.password.c_str(),
                               config_.database.c_str(),
                               config_.port,
                               nullptr,
                               0) == nullptr) {
            std::cerr << "[ERROR] MySqlConnectionPool: failed to connect MySQL: "
                      << mysql_error(mysql) << std::endl;
            mysql_close(mysql);
            for (MYSQL* connection : created) {
                mysql_close(connection);
            }
            stopped_ = true;
            return false;
        }

        created.push_back(mysql);
    }

    for (MYSQL* connection : created) {
        available_.push(connection);
        all_connections_.push_back(connection);
    }

    ready_ = true;
    return true;
}

PooledMySqlConnection MySqlConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] {
        return stopped_ || !available_.empty();
    });

    if (stopped_ || available_.empty()) {
        return {};
    }

    MYSQL* connection = available_.front();
    available_.pop();
    return PooledMySqlConnection(this, connection);
}

void MySqlConnectionPool::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ && all_connections_.empty()) {
        return;
    }

    stopped_ = true;
    ready_ = false;
    close_all_unlocked();
    cv_.notify_all();
}

bool MySqlConnectionPool::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_ && !stopped_;
}

void MySqlConnectionPool::release(MYSQL* connection) {
    if (connection == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        mysql_close(connection);
        return;
    }

    available_.push(connection);
    cv_.notify_one();
}

void MySqlConnectionPool::close_all_unlocked() {
    while (!available_.empty()) {
        MYSQL* connection = available_.front();
        if (connection != nullptr) {
            mysql_close(connection);
        }
        available_.pop();
    }

    all_connections_.clear();
}

} // namespace cpp_chat::storage
