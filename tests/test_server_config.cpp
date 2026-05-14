#include "cpp_chat/core/server_config.h"

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

namespace cpp_chat::core {

namespace {

class EnvGuard {
public:
    explicit EnvGuard(const char* name) : name_(name) {
        const char* value = std::getenv(name_);
        if (value != nullptr) {
            had_value_ = true;
            old_value_ = value;
        }
    }

    ~EnvGuard() {
        if (had_value_) {
            setenv(name_, old_value_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    bool had_value_ = false;
    std::string old_value_;
};

} // namespace

TEST(ServerConfig, BackpressureDefaultsAreExposed) {
    ServerConfig config;

    EXPECT_EQ(config.max_read_buffer_bytes, 2 * 1024 * 1024);
    EXPECT_EQ(config.max_write_buffer_bytes, 2 * 1024 * 1024);
    EXPECT_EQ(config.max_response_queue_size, 10000);
    EXPECT_EQ(config.max_thread_pool_queue_size, 10000);
}

TEST(ServerConfig, EnvironmentOverridesBackpressureLimits) {
    EnvGuard read_guard("CPP_CHAT_MAX_READ_BUFFER_BYTES");
    EnvGuard write_guard("CPP_CHAT_MAX_WRITE_BUFFER_BYTES");
    EnvGuard response_guard("CPP_CHAT_MAX_RESPONSE_QUEUE_SIZE");
    EnvGuard thread_guard("CPP_CHAT_THREAD_POOL_MAX_QUEUE_SIZE");

    setenv("CPP_CHAT_MAX_READ_BUFFER_BYTES", "4096", 1);
    setenv("CPP_CHAT_MAX_WRITE_BUFFER_BYTES", "8192", 1);
    setenv("CPP_CHAT_MAX_RESPONSE_QUEUE_SIZE", "32", 1);
    setenv("CPP_CHAT_THREAD_POOL_MAX_QUEUE_SIZE", "64", 1);

    const auto config = load_default_config();

    EXPECT_EQ(config.max_read_buffer_bytes, 4096);
    EXPECT_EQ(config.max_write_buffer_bytes, 8192);
    EXPECT_EQ(config.max_response_queue_size, 32);
    EXPECT_EQ(config.max_thread_pool_queue_size, 64);
}

TEST(ServerConfig, InvalidBackpressureEnvironmentKeepsDefaults) {
    EnvGuard read_guard("CPP_CHAT_MAX_READ_BUFFER_BYTES");
    EnvGuard response_guard("CPP_CHAT_MAX_RESPONSE_QUEUE_SIZE");

    setenv("CPP_CHAT_MAX_READ_BUFFER_BYTES", "-1", 1);
    setenv("CPP_CHAT_MAX_RESPONSE_QUEUE_SIZE", "bad", 1);

    const auto config = load_default_config();

    EXPECT_EQ(config.max_read_buffer_bytes, 2 * 1024 * 1024);
    EXPECT_EQ(config.max_response_queue_size, 10000);
}

} // namespace cpp_chat::core
