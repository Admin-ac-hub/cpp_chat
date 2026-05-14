#include "cpp_chat/storage/mysql_connection_pool.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mysql.h>
#include <utility>

namespace cpp_chat::storage {

namespace {

bool positive_pool_config(const MySqlConnectionPoolConfig& config) {
    return config.pool_size > 0 &&
           config.connect_timeout_seconds > 0 &&
           config.read_timeout_seconds > 0 &&
           config.write_timeout_seconds > 0 &&
           config.acquire_timeout_ms > 0 &&
           config.max_reconnect_attempts >= 0 &&
           config.idle_ping_interval_seconds > 0 &&
           config.idle_check_interval_seconds > 0;
}

} // namespace

PooledMySqlConnection::PooledMySqlConnection(MySqlConnectionPool* pool,
                                             PooledConnection* connection)
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
    return connection_ == nullptr ? nullptr : connection_->mysql;
}

PooledMySqlConnection::operator bool() const {
    return get() != nullptr;
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

    if (!positive_pool_config(config_)) {
        std::cerr << "[ERROR] MySqlConnectionPool: invalid pool config" << std::endl;
        stopped_ = true;
        return false;
    }

    std::vector<std::unique_ptr<PooledConnection>> created;
    created.reserve(static_cast<std::size_t>(config_.pool_size));
    for (int i = 0; i < config_.pool_size; ++i) {
        auto slot = std::make_unique<PooledConnection>();
        slot->mysql = create_connection();
        if (slot->mysql == nullptr) {
            std::cerr << "[ERROR] MySqlConnectionPool: failed to create initial connection"
                      << std::endl;
            for (auto& connection : created) {
                close_connection(*connection);
            }
            stopped_ = true;
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        slot->last_used = now;
        slot->last_checked = now;
        slot->generation = next_generation_++;
        created.push_back(std::move(slot));
    }

    for (auto& connection : created) {
        available_.push(connection.get());
        connections_.push_back(std::move(connection));
    }

    last_idle_check_ = std::chrono::steady_clock::now();
    ready_ = true;
    return true;
}

PooledMySqlConnection MySqlConnectionPool::acquire() {
    PooledConnection* connection = nullptr;
    std::vector<PooledConnection*> idle_check_candidates;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_check_candidates = idle_health_check_unlocked();
    }

    for (PooledConnection* candidate : idle_check_candidates) {
        if (candidate != nullptr) {
            ensure_healthy(*candidate);
            release(candidate);
        }
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto timeout = std::chrono::milliseconds(config_.acquire_timeout_ms);
        const bool has_connection = cv_.wait_for(lock, timeout, [this] {
            return stopped_ || !available_.empty();
        });

        if (!has_connection || stopped_ || available_.empty()) {
            return {};
        }

        connection = available_.front();
        available_.pop();
    }

    if (connection == nullptr) {
        return {};
    }

    if (!ensure_healthy(*connection)) {
        release(connection);
        return {};
    }

    return PooledMySqlConnection(this, connection);
}

void MySqlConnectionPool::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ && connections_.empty()) {
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

MYSQL* MySqlConnectionPool::create_connection() {
    MYSQL* mysql = mysql_init(nullptr);
    if (mysql == nullptr) {
        std::cerr << "[ERROR] MySqlConnectionPool: mysql_init failed" << std::endl;
        return nullptr;
    }

    const unsigned int connect_timeout = static_cast<unsigned int>(
        std::max(1, config_.connect_timeout_seconds));
    const unsigned int read_timeout = static_cast<unsigned int>(
        std::max(1, config_.read_timeout_seconds));
    const unsigned int write_timeout = static_cast<unsigned int>(
        std::max(1, config_.write_timeout_seconds));

    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
    mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &write_timeout);

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
        return nullptr;
    }

    return mysql;
}

void MySqlConnectionPool::close_connection(PooledConnection& connection) {
    if (connection.mysql != nullptr) {
        mysql_close(connection.mysql);
        connection.mysql = nullptr;
    }
}

bool MySqlConnectionPool::ping_connection(PooledConnection& connection) {
    if (connection.mysql == nullptr) {
        return false;
    }

    if (mysql_ping(connection.mysql) == 0) {
        connection.last_checked = std::chrono::steady_clock::now();
        return true;
    }

    std::cerr << "[WARN] MySqlConnectionPool: mysql_ping failed: "
              << mysql_error(connection.mysql) << std::endl;
    return false;
}

bool MySqlConnectionPool::reconnect_connection(PooledConnection& connection) {
    close_connection(connection);

    for (int attempt = 0; attempt < config_.max_reconnect_attempts; ++attempt) {
        MYSQL* mysql = create_connection();
        if (mysql != nullptr) {
            const auto now = std::chrono::steady_clock::now();
            connection.mysql = mysql;
            connection.last_used = now;
            connection.last_checked = now;
            connection.generation = next_generation_++;
            return true;
        }
    }

    std::cerr << "[ERROR] MySqlConnectionPool: failed to reconnect MySQL after "
              << config_.max_reconnect_attempts << " attempt(s)" << std::endl;
    return false;
}

bool MySqlConnectionPool::ensure_healthy(PooledConnection& connection) {
    const auto now = std::chrono::steady_clock::now();
    const auto ping_interval = std::chrono::seconds(config_.idle_ping_interval_seconds);
    const bool should_ping = connection.mysql == nullptr ||
                             (now - connection.last_checked) >= ping_interval;

    if (!should_ping) {
        return true;
    }

    if (ping_connection(connection)) {
        return true;
    }

    return reconnect_connection(connection);
}

void MySqlConnectionPool::release(PooledConnection* connection) {
    if (connection == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        close_connection(*connection);
        return;
    }

    connection->last_used = std::chrono::steady_clock::now();
    available_.push(connection);
    cv_.notify_one();
}

void MySqlConnectionPool::close_all_unlocked() {
    while (!available_.empty()) {
        available_.pop();
    }

    for (auto& connection : connections_) {
        if (connection != nullptr) {
            close_connection(*connection);
        }
    }
    connections_.clear();
}

std::vector<PooledConnection*> MySqlConnectionPool::idle_health_check_unlocked() {
    std::vector<PooledConnection*> candidates;
    if (stopped_ || available_.empty()) {
        return candidates;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto check_interval = std::chrono::seconds(config_.idle_check_interval_seconds);
    if (last_idle_check_ != std::chrono::steady_clock::time_point{} &&
        (now - last_idle_check_) < check_interval) {
        return candidates;
    }
    last_idle_check_ = now;

    const auto ping_interval = std::chrono::seconds(config_.idle_ping_interval_seconds);
    std::vector<PooledConnection*> keep;
    while (!available_.empty()) {
        PooledConnection* connection = available_.front();
        available_.pop();
        if (connection != nullptr &&
            (connection->mysql == nullptr || (now - connection->last_checked) >= ping_interval)) {
            candidates.push_back(connection);
        } else {
            keep.push_back(connection);
        }
    }

    for (PooledConnection* connection : keep) {
        available_.push(connection);
    }
    return candidates;
}

} // namespace cpp_chat::storage
