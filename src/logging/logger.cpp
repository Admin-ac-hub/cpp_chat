#include "cpp_chat/logging/logger.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mysql.h>
#include <sstream>
#include <utility>

namespace cpp_chat::logging {

namespace {

// 把枚举日志级别转换为数据库和控制台中使用的文本。
const char* level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

// 生成本地时区时间戳，用于控制台日志前缀。
std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
    // localtime_r 是线程安全版本，避免静态 tm 缓冲被并发覆盖。
    localtime_r(&now_time, &local_time);

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

// 对简单 SQL 语句做一层包装，让构造函数的错误处理更清晰。
bool execute_query(MYSQL* mysql, const char* query) {
    return mysql_query(mysql, query) == 0;
}

} // namespace

Logger::Logger(storage::MySqlConnectionPool& pool) : pool_(&pool) {
    auto connection = pool_->acquire();
    if (!connection) {
        std::cerr << "[ERROR] Logger: failed to acquire MySQL connection" << std::endl;
        return;
    }
    MYSQL* mysql = connection.get();

    // 日志表在启动时自动创建，部署时只需要保证库和用户存在。
    const char* create_table =
        "CREATE TABLE IF NOT EXISTS server_logs ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "level VARCHAR(16) NOT NULL,"
        "message TEXT NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (!execute_query(mysql, create_table)) {
        std::cerr << "[ERROR] failed to create MySQL log table: " << mysql_error(mysql) << std::endl;
    }
}

void Logger::info(const std::string& message) {
    write(LogLevel::Info, message);
}

void Logger::warn(const std::string& message) {
    write(LogLevel::Warn, message);
}

void Logger::error(const std::string& message) {
    write(LogLevel::Error, message);
}

void Logger::write(LogLevel level, const std::string& message) {
    const char* level_name = level_to_string(level);
    const std::string line = current_timestamp() + " [" + level_name + "] " + message;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << line << std::endl;
    }

    // 数据库不可用时直接退化为控制台日志，不影响主业务流程。
    if (pool_ == nullptr) {
        return;
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return;
    }
    MYSQL* mysql = connection.get();

    // 使用预处理语句写日志，避免 message 中包含引号等字符时破坏 SQL。
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        std::cerr << "[ERROR] mysql_stmt_init failed" << std::endl;
        return;
    }

    const char* query = "INSERT INTO server_logs (level, message) VALUES (?, ?)";
    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] failed to prepare MySQL log insert: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return;
    }

    MYSQL_BIND bind[2]{};
    unsigned long level_length = static_cast<unsigned long>(std::strlen(level_name));
    unsigned long message_length = static_cast<unsigned long>(message.size());

    // 第一个参数绑定日志级别字符串。
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(level_name);
    bind[0].buffer_length = level_length;
    bind[0].length = &level_length;

    // 第二个参数绑定日志正文，允许包含任意普通文本。
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = const_cast<char*>(message.data());
    bind[1].buffer_length = message_length;
    bind[1].length = &message_length;

    if (mysql_stmt_bind_param(statement, bind) != 0) {
        std::cerr << "[ERROR] failed to bind MySQL log insert: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return;
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] failed to execute MySQL log insert: "
                  << mysql_stmt_error(statement) << std::endl;
    }

    mysql_stmt_close(statement);
}

} // namespace cpp_chat::logging
