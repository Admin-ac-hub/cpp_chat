#pragma once

#include "cpp_chat/logging/logger.h"
#include "cpp_chat/network/connection.h"
#include "cpp_chat/protocol/message.h"
#include "cpp_chat/session/session_manager.h"
#include "cpp_chat/storage/message_store.h"
#include "cpp_chat/storage/user_store.h"

#include <string>
#include <vector>

namespace cpp_chat::chat {

// ChatService 处理完一条客户端输入后，可能需要把响应发给一个或多个连接。
// 例如私聊成功时，需要同时通知接收方并给发送方返回确认。
struct OutboundMessage {
    network::ConnectionId connection_id = 0;
    std::string data;
    bool close_after_send = false;
};

// 聊天业务层。
//
// 这个类不直接操作 socket，也不关心 epoll 细节；它只负责：
// 1. 解析客户端命令。
// 2. 维护登录会话。
// 3. 持久化或暂存消息。
// 4. 生成网络层需要发送的响应内容。
class ChatService {
public:
    // 依赖由外部注入，便于网络层、会话层、存储层职责分离。
    ChatService(session::SessionManager& sessions,
                storage::MessageStore& message_store,
                storage::UserStore& user_store,
                logging::Logger& logger);

    // 直接处理一条已经构造好的消息，当前主要用于存储和日志记录。
    void handle_message(const protocol::Message& message);

    // 处理客户端的一整行文本命令。
    //
    // connection_id 用来识别命令来自哪个连接；line 不包含末尾换行。
    // 返回值是需要写回 socket 的消息列表，网络层按 connection_id 分发。
    std::vector<OutboundMessage> handle_client_line(network::ConnectionId connection_id,
                                                    const std::string& line);

    // 客户端断开后清理该连接绑定的登录态。
    void handle_disconnect(network::ConnectionId connection_id);

private:
    // 当前在线用户与连接的双向索引。
    session::SessionManager& sessions_;

    // 简单消息存储，当前是内存实现。
    storage::MessageStore& message_store_;

    // 用户注册、登录和用户名/user_id 查询。
    storage::UserStore& user_store_;

    // 统一日志入口，同时写 stdout 和 MySQL。
    logging::Logger& logger_;
};

} // namespace cpp_chat::chat
