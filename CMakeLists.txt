cmake_minimum_required(VERSION 3.16)

project(cpp_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp_chat_server
    src/main.cpp
    src/app/chat_server_app.cpp
    src/chat/chat_service.cpp
    src/core/server_config.cpp
    src/logging/logger.cpp
    src/network/tcp_server.cpp
    src/protocol/message.cpp
    src/session/session_manager.cpp
    src/storage/message_store.cpp
)

target_include_directories(cpp_chat_server
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
