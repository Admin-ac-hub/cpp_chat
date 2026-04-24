#pragma once

#include <string>

namespace cpp_chat::logging {

enum class LogLevel {
    Info,
    Warn,
    Error
};

class Logger {
public:
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    void write(LogLevel level, const std::string& message);
};

} // namespace cpp_chat::logging

