cmake_minimum_required(VERSION 3.16)

project(cpp_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MYSQLCLIENT QUIET mysqlclient)
    if(NOT MYSQLCLIENT_FOUND)
        pkg_check_modules(MYSQLCLIENT QUIET mariadb)
    endif()
endif()

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

if(NOT MYSQLCLIENT_FOUND)
    message(FATAL_ERROR "MySQL client library not found. Install mysqlclient or mariadb development package.")
endif()

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
        ${MYSQLCLIENT_INCLUDE_DIRS}
)

target_link_libraries(cpp_chat_server
    PRIVATE
        ${MYSQLCLIENT_LIBRARIES}
)
