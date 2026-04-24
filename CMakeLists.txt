cmake_minimum_required(VERSION 3.16)

project(cpp_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp_chat_server
    src/main.cpp
)

target_include_directories(cpp_chat_server
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

