#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::core {

struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 9000;
    std::string log_file = "logs/server.log";
};

ServerConfig load_default_config();

} // namespace cpp_chat::core

