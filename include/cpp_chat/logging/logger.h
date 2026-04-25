#pragma once

#include <cstdint>
#include <mutex>
#include <string>

struct MYSQL;

namespace cpp_chat::logging {

enum class LogLevel {
    Info,
    Warn,
    Error
};

struct MySqlLogConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string user = "cpp_chat";
    std::string password = "cpp_chat";
    std::string database = "cpp_chat";
};

class Logger {
public:
    Logger() = default;
    explicit Logger(MySqlLogConfig config);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    void write(LogLevel level, const std::string& message);

    std::mutex mutex_;
    MYSQL* mysql_ = nullptr;
    MySqlLogConfig config_;
};

} // namespace cpp_chat::logging
