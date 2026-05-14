#pragma once

#include "cpp_chat/chat/chat_service.h"
#include "cpp_chat/core/server_config.h"
#include "cpp_chat/core/thread_pool.h"
#include "cpp_chat/logging/logger.h"
#include "cpp_chat/network/tcp_server.h"
#include "cpp_chat/session/session_manager.h"
#include "cpp_chat/storage/group_member_store.h"
#include "cpp_chat/storage/group_store.h"
#include "cpp_chat/storage/message_store.h"
#include "cpp_chat/storage/mysql_connection_pool.h"
#include "cpp_chat/storage/user_store.h"

namespace cpp_chat::app {

// 应用装配层。
//
// ChatServerApp 负责把配置、日志、存储、会话、业务服务和 TCP 服务器串起来。
// 具体业务逻辑仍然放在 ChatService，网络细节放在 TcpServer。
class ChatServerApp {
public:
    // 使用配置创建所有服务组件。
    explicit ChatServerApp(core::ServerConfig config);

    // 启动服务器并返回进程退出码。
    int run();

private:
    // 服务运行配置，包括监听地址、端口和日志数据库连接信息。
    core::ServerConfig config_;

    // 共享 MySQL 连接池必须先于所有数据库使用方构造，并最后析构。
    storage::MySqlConnectionPool db_pool_;

    // 日志组件依赖共享 MySQL 连接池，所以需要先于依赖它的组件构造。
    logging::Logger logger_;

    // 消息存储和会话管理是业务层依赖。
    storage::MessageStore message_store_;
    storage::UserStore user_store_;
    storage::GroupStore group_store_;
    storage::GroupMemberStore group_member_store_;
    session::SessionManager session_manager_;

    // 工作线程池需要先于 TcpServer 构造，供网络层投递业务任务。
    core::ThreadPool thread_pool_;

    // 业务层依赖 session/message/user/logging，网络层再依赖业务层和线程池。
    chat::ChatService chat_service_;
    network::TcpServer tcp_server_;
};

} // namespace cpp_chat::app
