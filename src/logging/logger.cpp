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

std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
    localtime_r(&now_time, &local_time);

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

bool execute_query(MYSQL* mysql, const char* query) {
    return mysql_query(mysql, query) == 0;
}

} // namespace

Logger::Logger(MySqlLogConfig config) : config_(std::move(config)) {
    mysql_ = mysql_init(nullptr);
    if (mysql_ == nullptr) {
        std::cerr << "[ERROR] mysql_init failed" << std::endl;
        return;
    }

    if (mysql_real_connect(mysql_,
                           config_.host.c_str(),
                           config_.user.c_str(),
                           config_.password.c_str(),
                           config_.database.c_str(),
                           config_.port,
                           nullptr,
                           0) == nullptr) {
        std::cerr << "[ERROR] failed to connect MySQL logger: " << mysql_error(mysql_) << std::endl;
        mysql_close(mysql_);
        mysql_ = nullptr;
        return;
    }

    const char* create_table =
        "CREATE TABLE IF NOT EXISTS server_logs ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "level VARCHAR(16) NOT NULL,"
        "message TEXT NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (!execute_query(mysql_, create_table)) {
        std::cerr << "[ERROR] failed to create MySQL log table: " << mysql_error(mysql_) << std::endl;
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

Logger::~Logger() {
    if (mysql_ != nullptr) {
        mysql_close(mysql_);
        mysql_ = nullptr;
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
    std::lock_guard<std::mutex> lock(mutex_);

    const char* level_name = level_to_string(level);
    const std::string line = current_timestamp() + " [" + level_name + "] " + message;
    std::cout << line << std::endl;

    if (mysql_ == nullptr) {
        return;
    }

    MYSQL_STMT* statement = mysql_stmt_init(mysql_);
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

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(level_name);
    bind[0].buffer_length = level_length;
    bind[0].length = &level_length;

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
