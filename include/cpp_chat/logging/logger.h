#pragma once

#include "cpp_chat/storage/mysql_connection_pool.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace cpp_chat::logging {

// 日志级别，当前用于控制输出文本和写入 MySQL 的 level 字段。
enum class LogLevel {
    Info,
    Warn,
    Error
};

// 线程安全日志组件。
//
// 每条日志都会输出到 stdout；如果 MySQL 初始化成功，也会写入 server_logs 表。
// MySQL 写入失败不会阻止服务器继续运行，只退化为控制台日志。
class Logger {
public:
    // 默认构造不连接 MySQL，适合测试或不需要数据库日志的场景。
    Logger() = default;

    // 使用共享连接池写 MySQL 日志，并创建日志表。
    explicit Logger(storage::MySqlConnectionPool& pool);

    ~Logger() = default;

    // Logger 持有连接池引用，禁止拷贝，避免悬空引用和并发语义不清。
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 便捷日志入口。
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    // 统一写日志实现，负责加锁、格式化、控制台输出和 MySQL 插入。
    void write(LogLevel level, const std::string& message);

    // 保护 stdout 输出，避免多线程写日志时交叉。
    std::mutex mutex_;

    // 共享 MySQL 连接池；nullptr 表示数据库日志不可用。
    storage::MySqlConnectionPool* pool_ = nullptr;
};

} // namespace cpp_chat::logging
