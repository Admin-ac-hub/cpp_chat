#pragma once

#include <cstdint>
#include <string>

namespace cpp_chat::protocol {

// 客户端和服务端内部统一使用的消息类型。
// 当前文本协议只实现了 Login 和 DirectChat，其余类型预留给群聊、历史查询等功能。
enum class MessageType {
    Register,
    Login,
    Logout,
    DirectChat,
    GroupChat,
    CreateGroup,
    GroupJoin,
    GroupLeave,
    HistoryQuery,
    UnreadQuery,
    Ping,
    System
};

// 服务内部流转的标准消息对象。
// sender_id 和 receiver_id 都是业务用户 ID，不是 socket fd。
struct Message {
    MessageType type = MessageType::System;
    std::uint64_t sender_id = 0;
    std::uint64_t receiver_id = 0;
    std::string body;
};

// 从客户端文本命令解析出来的中间结构。
// 不同命令使用不同字段组合，详见 parse_client_command 注释。
struct ClientCommand {
    MessageType type = MessageType::System;
    std::uint64_t user_id = 0;
    std::uint64_t receiver_id = 0;
    std::uint64_t group_id = 0;
    std::uint64_t last_seen_message_id = 0;
    std::uint32_t limit = 20;
    std::uint64_t before_id = 0;
    std::string username;
    std::string password;
    std::string target_username;
    std::string group_name;
    std::string body;
    bool group_history = false;  // HISTORY GC 时为 true
};

// 将消息类型转换为日志友好的字符串。
std::string to_string(MessageType type);

// 解析一行客户端 JSON 命令。
//
// 支持格式：
// - {"type":"register","username":"ys","password":"123456"}
// - {"type":"login","username":"ys","password":"123456"}
// - {"type":"logout"}
// - {"type":"ping"}
// - {"type":"dm","to":"bob","body":"hello"}
// - {"type":"create_group","name":"backend"}
// - {"type":"join_group","group_id":100}
// - {"type":"leave_group","group_id":100}
// - {"type":"group_message","group_id":100,"body":"hello"}
// - {"type":"history","peer":"bob"}
// - {"type":"group_history","group_id":100}
// - {"type":"unread","last_seen_message_id":123,"limit":20}
//
// 成功时填充 command 并返回 true；失败时填充 error，调用方负责返回给客户端。
bool parse_client_command(const std::string& line, ClientCommand& command, std::string& error);

// 格式化服务端 JSON payload；网络层负责添加长度前缀。
std::string format_ok(const std::string& message);
std::string format_error(const std::string& message);
std::string format_register_success(std::uint64_t user_id);
std::string format_register_failed(const std::string& reason);
std::string format_login_success(std::uint64_t user_id, const std::string& username);
std::string format_login_failed(const std::string& reason);
std::string format_create_group_success(std::uint64_t group_id, const std::string& name);
std::string format_message_ack(std::uint64_t message_id, bool stored, bool delivered);

// 格式化私聊投递消息：{"type":"dm","from":"alice","body":"hello"}
std::string format_direct_message(const std::string& sender_username, const std::string& body);

// 格式化群聊投递消息。
std::string format_group_message(std::uint64_t group_id, const std::string& sender_username,
                                 const std::string& body);

// 格式化历史记录单条。
std::string format_history_item(const std::string& chat_type,
                                const std::string& from,
                                const std::string& to,
                                const std::string& body);
std::string format_history_item(std::uint64_t message_id,
                                const std::string& chat_type,
                                const std::string& from,
                                const std::string& to,
                                const std::string& body,
                                const std::string& created_at);

// 格式化历史记录结束标记。
std::string format_history_end();
std::string format_history_end(bool has_more, std::uint64_t next_before_id);
std::string format_unread_end(bool has_more, std::uint64_t next_last_seen_message_id);

// 格式化心跳响应。
std::string format_pong();

} // namespace cpp_chat::protocol
