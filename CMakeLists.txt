cmake_minimum_required(VERSION 3.16)

# 项目只包含 C++ 服务端程序。
project(cpp_chat LANGUAGES CXX)

# 代码使用 std::optional、std::string_view 兼容的 C++17 基础能力。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 优先通过 pkg-config 查找 mysqlclient/mariadb 开发包。
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MYSQLCLIENT QUIET mysqlclient)
    if(NOT MYSQLCLIENT_FOUND)
        pkg_check_modules(MYSQLCLIENT QUIET mariadb)
    endif()
endif()

# 如果 pkg-config 不可用，再手动查找头文件和动态库。
if(NOT MYSQLCLIENT_FOUND)
    find_path(MYSQLCLIENT_INCLUDE_DIR
        NAMES mysql.h
        PATH_SUFFIXES mysql mariadb
    )
    find_library(MYSQLCLIENT_LIBRARY
        NAMES mysqlclient mariadb
    )

    if(MYSQLCLIENT_INCLUDE_DIR AND MYSQLCLIENT_LIBRARY)
        set(MYSQLCLIENT_FOUND TRUE)
        set(MYSQLCLIENT_INCLUDE_DIRS ${MYSQLCLIENT_INCLUDE_DIR})
        set(MYSQLCLIENT_LIBRARIES ${MYSQLCLIENT_LIBRARY})
    endif()
endif()

# 日志模块依赖 MySQL C API；缺少开发库时直接停止配置，避免链接阶段才失败。
if(NOT MYSQLCLIENT_FOUND)
    message(FATAL_ERROR "MySQL client library not found. Install mysqlclient or mariadb development package.")
endif()

find_package(OpenSSL REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
# 公共业务逻辑库（OBJECT 库）
#
# 把所有业务源文件单独编译成对象集合，让 cpp_chat_server 和 cpp_chat_tests
# 各自链接同一份对象，避免重复编译，也避免把 main() 混入测试目标。
# ─────────────────────────────────────────────────────────────────────────────
add_library(cpp_chat_lib OBJECT
    src/app/chat_server_app.cpp
    src/chat/chat_service.cpp
    src/core/server_config.cpp
    src/core/thread_pool.cpp
    src/logging/logger.cpp
    src/network/packet_codec.cpp
    src/network/tcp_server.cpp
    src/protocol/message.cpp
    src/session/session_manager.cpp
    src/storage/group_store.cpp
    src/storage/group_member_store.cpp
    src/storage/message_store.cpp
    src/storage/mysql_connection_pool.cpp
    src/storage/user_store.cpp
)

target_include_directories(cpp_chat_lib
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${MYSQLCLIENT_INCLUDE_DIRS}
)

target_link_libraries(cpp_chat_lib
    PUBLIC
        ${MYSQLCLIENT_LIBRARIES}
        OpenSSL::Crypto
)

# ─────────────────────────────────────────────────────────────────────────────
# 主服务端可执行文件
# ─────────────────────────────────────────────────────────────────────────────
add_executable(cpp_chat_server src/main.cpp)

target_link_libraries(cpp_chat_server
    PRIVATE
        cpp_chat_lib
)

# ─────────────────────────────────────────────────────────────────────────────
# 单元测试
#
# GoogleTest 通过 find_package 引入。
# 在 Docker（Linux）环境中，所有模块（含 epoll 网络层）均可正常编译。
# 如果 GoogleTest 未安装，测试目标会被跳过，主程序的构建不受影响。
# ─────────────────────────────────────────────────────────────────────────────
find_package(GTest QUIET)

if(GTest_FOUND)
    enable_testing()

    add_executable(cpp_chat_tests
        tests/test_protocol.cpp
        tests/test_packet_codec.cpp
        tests/test_session.cpp
        tests/test_group_store.cpp
        tests/test_group_member_store.cpp
        tests/test_message_store.cpp
        tests/test_mysql_connection_pool.cpp
        tests/test_server_config.cpp
        tests/test_user_store.cpp
        tests/test_thread_pool.cpp
        tests/test_chat_service.cpp
    )

    target_link_libraries(cpp_chat_tests
        PRIVATE
            cpp_chat_lib
            GTest::gtest
            GTest::gtest_main
    )

    # 让 CTest 自动发现并注册 GoogleTest 用例，可以直接用 ctest 运行。
    include(GoogleTest)
    gtest_discover_tests(cpp_chat_tests)

    message(STATUS "GoogleTest found — test target 'cpp_chat_tests' enabled.")
else()
    message(STATUS "GoogleTest not found — skipping test target.")
endif()
