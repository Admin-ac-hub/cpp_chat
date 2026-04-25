#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::core {

struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 9000;
    std::string log_db_host = "127.0.0.1";
    std::uint16_t log_db_port = 3306;
    std::string log_db_user = "cpp_chat";
    std::string log_db_password = "cpp_chat";
    std::string log_db_name = "cpp_chat";
};

ServerConfig load_default_config();

} // namespace cpp_chat::core
