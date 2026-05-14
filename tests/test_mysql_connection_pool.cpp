#include "cpp_chat/storage/mysql_connection_pool.h"

#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <mysql.h>
#include <string>
#include <thread>

using cpp_chat::storage::MySqlConnectionPool;
using cpp_chat::storage::MySqlConnectionPoolConfig;

namespace {

bool mysql_pool_integration_enabled() {
    const char* value = std::getenv("CPP_CHAT_TEST_MYSQL");
    return value != nullptr && std::string(value) == "1";
}

std::string env_or(const char* name, const std::string& default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }
    return value;
}

std::uint16_t env_port_or(const char* name, std::uint16_t default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return default_value;
    }
    return static_cast<std::uint16_t>(std::stoi(value));
}

MySqlConnectionPoolConfig integration_config() {
    MySqlConnectionPoolConfig config;
    config.host = env_or("CPP_CHAT_DB_HOST", env_or("CPP_CHAT_LOG_DB_HOST", config.host));
    config.port = env_port_or("CPP_CHAT_DB_PORT",
        env_port_or("CPP_CHAT_LOG_DB_PORT", config.port));
    config.user = env_or("CPP_CHAT_DB_USER", env_or("CPP_CHAT_LOG_DB_USER", config.user));
    config.password = env_or("CPP_CHAT_DB_PASSWORD",
        env_or("CPP_CHAT_LOG_DB_PASSWORD", config.password));
    config.database = env_or("CPP_CHAT_DB_NAME", env_or("CPP_CHAT_LOG_DB_NAME", config.database));
    config.pool_size = 1;
    config.acquire_timeout_ms = 50;
    config.connect_timeout_seconds = 2;
    config.read_timeout_seconds = 2;
    config.write_timeout_seconds = 2;
    config.max_reconnect_attempts = 2;
    config.idle_ping_interval_seconds = 1;
    config.idle_check_interval_seconds = 1;
    return config;
}

MYSQL* open_mysql_connection(const MySqlConnectionPoolConfig& config) {
    MYSQL* mysql = mysql_init(nullptr);
    if (mysql == nullptr) {
        return nullptr;
    }

    const unsigned int timeout = 2;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout);

    if (mysql_real_connect(mysql,
                           config.host.c_str(),
                           config.user.c_str(),
                           config.password.c_str(),
                           config.database.c_str(),
                           config.port,
                           nullptr,
                           0) == nullptr) {
        mysql_close(mysql);
        return nullptr;
    }
    return mysql;
}

bool query_select_one(MYSQL* mysql) {
    if (mysql_query(mysql, "SELECT 1") != 0) {
        return false;
    }
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result == nullptr) {
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    const bool ok = row != nullptr && row[0] != nullptr && std::string(row[0]) == "1";
    mysql_free_result(result);
    return ok;
}

} // namespace

TEST(MySqlConnectionPool, DefaultPoolIsNotReadyAndAcquireReturnsEmpty) {
    MySqlConnectionPool pool;

    EXPECT_FALSE(pool.ready());
    auto connection = pool.acquire();
    EXPECT_FALSE(connection);
}

TEST(MySqlConnectionPool, RejectsNonPositivePoolSize) {
    MySqlConnectionPool pool;
    MySqlConnectionPoolConfig config;
    config.pool_size = 0;

    EXPECT_FALSE(pool.init(config));
    EXPECT_FALSE(pool.ready());
}

TEST(MySqlConnectionPool, RejectsInvalidHealthCheckConfig) {
    MySqlConnectionPool pool;
    MySqlConnectionPoolConfig config;
    config.acquire_timeout_ms = 0;

    EXPECT_FALSE(pool.init(config));
    EXPECT_FALSE(pool.ready());
}

TEST(MySqlConnectionPool, ConfigExposesReconnectAndTimeoutDefaults) {
    MySqlConnectionPoolConfig config;

    EXPECT_EQ(config.connect_timeout_seconds, 3);
    EXPECT_EQ(config.read_timeout_seconds, 5);
    EXPECT_EQ(config.write_timeout_seconds, 5);
    EXPECT_EQ(config.acquire_timeout_ms, 3000);
    EXPECT_EQ(config.max_reconnect_attempts, 3);
    EXPECT_EQ(config.idle_ping_interval_seconds, 30);
    EXPECT_EQ(config.idle_check_interval_seconds, 10);
}

TEST(MySqlConnectionPool, FailedInitLeavesPoolStopped) {
    MySqlConnectionPool pool;
    MySqlConnectionPoolConfig config;
    config.host = "127.0.0.1";
    config.port = 1;
    config.pool_size = 1;

    EXPECT_FALSE(pool.init(config));
    EXPECT_FALSE(pool.ready());

    auto connection = pool.acquire();
    EXPECT_FALSE(connection);
}

TEST(MySqlConnectionPool, ShutdownIsIdempotent) {
    MySqlConnectionPool pool;

    pool.shutdown();
    pool.shutdown();

    EXPECT_FALSE(pool.ready());
}

TEST(MySqlConnectionPool, AcquireReleaseReusesPoolSlotWithRealMySql) {
    if (!mysql_pool_integration_enabled()) {
        GTEST_SKIP() << "set CPP_CHAT_TEST_MYSQL=1 to run MySQL integration tests";
    }

    MySqlConnectionPool pool;
    const auto config = integration_config();
    ASSERT_TRUE(pool.init(config));
    ASSERT_TRUE(pool.ready());

    auto first = pool.acquire();
    ASSERT_TRUE(first);
    EXPECT_TRUE(query_select_one(first.get()));

    auto blocked = pool.acquire();
    EXPECT_FALSE(blocked);

    first = {};
    auto second = pool.acquire();
    ASSERT_TRUE(second);
    EXPECT_TRUE(query_select_one(second.get()));
}

TEST(MySqlConnectionPool, ReconnectsAfterKilledConnectionWithRealMySql) {
    if (!mysql_pool_integration_enabled()) {
        GTEST_SKIP() << "set CPP_CHAT_TEST_MYSQL=1 to run MySQL integration tests";
    }

    MySqlConnectionPool pool;
    const auto config = integration_config();
    ASSERT_TRUE(pool.init(config));

    unsigned long thread_id = 0;
    {
        auto connection = pool.acquire();
        ASSERT_TRUE(connection);
        ASSERT_TRUE(query_select_one(connection.get()));
        thread_id = mysql_thread_id(connection.get());
        ASSERT_NE(thread_id, 0ul);
    }

    MYSQL* killer = open_mysql_connection(config);
    ASSERT_NE(killer, nullptr);
    const std::string kill_query = "KILL " + std::to_string(thread_id);
    ASSERT_EQ(mysql_query(killer, kill_query.c_str()), 0) << mysql_error(killer);
    mysql_close(killer);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    auto reconnected = pool.acquire();
    ASSERT_TRUE(reconnected);
    EXPECT_TRUE(query_select_one(reconnected.get()));
    EXPECT_NE(mysql_thread_id(reconnected.get()), thread_id);
}
