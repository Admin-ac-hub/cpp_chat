#include "cpp_chat/storage/mysql_connection_pool.h"

#include <gtest/gtest.h>

using cpp_chat::storage::MySqlConnectionPool;
using cpp_chat::storage::MySqlConnectionPoolConfig;

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
