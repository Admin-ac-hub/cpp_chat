#include "cpp_chat/logging/logger.h"

#include <iostream>

namespace cpp_chat::logging {

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
    const char* level_name = "INFO";
    if (level == LogLevel::Warn) {
        level_name = "WARN";
    } else if (level == LogLevel::Error) {
        level_name = "ERROR";
    }

    std::cout << "[" << level_name << "] " << message << std::endl;
}

} // namespace cpp_chat::logging

