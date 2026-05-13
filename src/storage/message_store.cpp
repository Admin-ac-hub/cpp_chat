#include "cpp_chat/storage/message_store.h"

#include <cstring>
#include <iostream>
#include <mysql.h>
#include <utility>

namespace cpp_chat::storage {

namespace {

// 将 MessageType 转换为写入数据库 type 列的字符串。
const char* message_type_to_string(protocol::MessageType type) {
    switch (type) {
        case protocol::MessageType::DirectChat:
            return "DirectChat";
        case protocol::MessageType::GroupChat:
            return "GroupChat";
        case protocol::MessageType::GroupJoin:
            return "GroupJoin";
        case protocol::MessageType::GroupLeave:
            return "GroupLeave";
        default:
            return "System";
    }
}

// 对简单 SQL 执行一层包装，让构造函数的错误处理更清晰。
bool execute_query(MYSQL* mysql, const char* query) {
    return mysql_query(mysql, query) == 0;
}

// SELECT 查询失败时返回空结果并保留服务可用性，和 append 的降级策略一致。
std::vector<protocol::Message> execute_history_query(MYSQL* mysql,
                                                      const char* query,
                                                      protocol::MessageType type,
                                                      const std::vector<std::uint64_t>& query_params) {
    std::vector<protocol::Message> messages;

    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        std::cerr << "[ERROR] MessageStore: mysql_stmt_init failed for history query" << std::endl;
        return messages;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to prepare history query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return messages;
    }

    std::vector<MYSQL_BIND> params(query_params.size());
    for (std::size_t i = 0; i < query_params.size(); ++i) {
        params[i].buffer_type = MYSQL_TYPE_LONGLONG;
        params[i].buffer = const_cast<std::uint64_t*>(&query_params[i]);
        params[i].is_unsigned = true;
    }

    if (!params.empty()) {
        if (mysql_stmt_bind_param(statement, params.data()) != 0) {
            std::cerr << "[ERROR] MessageStore: failed to bind history query: "
                      << mysql_stmt_error(statement) << std::endl;
            mysql_stmt_close(statement);
            return messages;
        }
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to execute history query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return messages;
    }

    MYSQL_BIND results[3]{};
    std::uint64_t sender_id = 0;
    std::uint64_t receiver_id = 0;
    char body_probe = '\0';
    unsigned long body_length = 0;
    bool body_is_null = false;

    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &sender_id;
    results[0].is_unsigned = true;

    results[1].buffer_type = MYSQL_TYPE_LONGLONG;
    results[1].buffer = &receiver_id;
    results[1].is_unsigned = true;

    // TEXT 长度不固定，先用 0 长度缓冲拿到实际长度，再 fetch_column 取正文。
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = &body_probe;
    results[2].buffer_length = 0;
    results[2].length = &body_length;
    results[2].is_null = &body_is_null;

    if (mysql_stmt_bind_result(statement, results) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to bind history result: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return messages;
    }

    while (true) {
        const int fetch_status = mysql_stmt_fetch(statement);
        if (fetch_status == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
            std::cerr << "[ERROR] MessageStore: failed to fetch history row: "
                      << mysql_stmt_error(statement) << std::endl;
            break;
        }

        std::string body;
        if (!body_is_null && body_length > 0) {
            body.resize(body_length);
            MYSQL_BIND body_result{};
            body_result.buffer_type = MYSQL_TYPE_STRING;
            body_result.buffer = body.data();
            body_result.buffer_length = static_cast<unsigned long>(body.size());
            body_result.length = &body_length;

            if (mysql_stmt_fetch_column(statement, &body_result, 2, 0) != 0) {
                std::cerr << "[ERROR] MessageStore: failed to fetch history body: "
                          << mysql_stmt_error(statement) << std::endl;
                continue;
            }
        }

        messages.push_back({type, sender_id, receiver_id, body});
    }

    mysql_stmt_close(statement);
    return messages;
}

} // namespace

MessageStore::MessageStore(MySqlConnectionPool& pool) : pool_(&pool) {
    auto connection = pool_->acquire();
    if (!connection) {
        std::cerr << "[ERROR] MessageStore: failed to acquire MySQL connection" << std::endl;
        return;
    }
    MYSQL* mysql = connection.get();

    // messages 表在启动时自动创建，部署时只需保证库和用户存在。
    const char* create_table =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "created_at  TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "type        VARCHAR(32)     NOT NULL,"
        "sender_id   BIGINT UNSIGNED NOT NULL,"
        "receiver_id BIGINT UNSIGNED NOT NULL,"
        "body        TEXT            NOT NULL,"
        "INDEX idx_messages_sender   (sender_id),"
        "INDEX idx_messages_receiver (receiver_id),"
        "INDEX idx_messages_created  (created_at)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (!execute_query(mysql, create_table)) {
        std::cerr << "[ERROR] MessageStore: failed to create messages table: "
                  << mysql_error(mysql) << std::endl;
    }
}

void MessageStore::append(protocol::Message message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 先写内存，保证当次运行期间 all() 可以快速返回。
        messages_.push_back(message);
    }

    // 数据库不可用时退化为内存模式，不影响主业务流程。
    if (pool_ == nullptr) {
        return;
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return;
    }
    MYSQL* mysql = connection.get();

    // 使用预处理语句写消息，防止 body 中包含特殊字符时破坏 SQL。
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        std::cerr << "[ERROR] MessageStore: mysql_stmt_init failed" << std::endl;
        return;
    }

    const char* query =
        "INSERT INTO messages (type, sender_id, receiver_id, body) VALUES (?, ?, ?, ?)";
    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to prepare insert: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return;
    }

    const char* type_str = message_type_to_string(message.type);
    unsigned long type_length = static_cast<unsigned long>(std::strlen(type_str));
    unsigned long body_length = static_cast<unsigned long>(message.body.size());

    MYSQL_BIND bind[4]{};

    // type: 消息类型字符串（DirectChat / GroupChat / System）。
    bind[0].buffer_type   = MYSQL_TYPE_STRING;
    bind[0].buffer        = const_cast<char*>(type_str);
    bind[0].buffer_length = type_length;
    bind[0].length        = &type_length;

    // sender_id: 发送方业务用户 ID。
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer      = &message.sender_id;
    bind[1].is_unsigned = true;

    // receiver_id: 接收方业务用户 ID（私聊）或群组 ID（群聊）。
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer      = &message.receiver_id;
    bind[2].is_unsigned = true;

    // body: 消息正文，支持任意 UTF-8 文本。
    bind[3].buffer_type   = MYSQL_TYPE_STRING;
    bind[3].buffer        = const_cast<char*>(message.body.data());
    bind[3].buffer_length = body_length;
    bind[3].length        = &body_length;

    if (mysql_stmt_bind_param(statement, bind) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to bind insert: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return;
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to execute insert: "
                  << mysql_stmt_error(statement) << std::endl;
    }

    mysql_stmt_close(statement);
}

std::vector<protocol::Message> MessageStore::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // 返回内存副本；服务重启后内存为空，历史消息仅在 MySQL 中保留。
    return messages_;
}

std::vector<protocol::Message> MessageStore::direct_history(std::uint64_t user_id,
                                                            std::uint64_t peer_id) const {
    if (pool_ != nullptr) {
        auto connection = pool_->acquire();
        if (connection) {
            MYSQL* mysql = connection.get();
            const char* query =
                "SELECT sender_id, receiver_id, body "
                "FROM messages "
                "WHERE type = 'DirectChat' "
                "AND ((sender_id = ? AND receiver_id = ?) "
                "OR (sender_id = ? AND receiver_id = ?)) "
                "ORDER BY id ASC";

            const auto messages = execute_history_query(
                mysql,
                query,
                protocol::MessageType::DirectChat,
                {user_id, peer_id, peer_id, user_id});
            return messages;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<protocol::Message> result;
    for (const auto& msg : messages_) {
        if (msg.type == protocol::MessageType::DirectChat &&
            ((msg.sender_id == user_id && msg.receiver_id == peer_id) ||
             (msg.sender_id == peer_id && msg.receiver_id == user_id))) {
            result.push_back(msg);
        }
    }
    return result;
}

std::vector<protocol::Message> MessageStore::group_history(std::uint64_t group_id) const {
    if (pool_ != nullptr) {
        auto connection = pool_->acquire();
        if (connection) {
            MYSQL* mysql = connection.get();
            const char* query =
                "SELECT sender_id, receiver_id, body "
                "FROM messages "
                "WHERE type = 'GroupChat' AND receiver_id = ? "
                "ORDER BY id ASC";

            const auto messages = execute_history_query(
                mysql, query, protocol::MessageType::GroupChat, {group_id});
            return messages;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<protocol::Message> result;
    for (const auto& msg : messages_) {
        if (msg.type == protocol::MessageType::GroupChat && msg.receiver_id == group_id) {
            result.push_back(msg);
        }
    }
    return result;
}

} // namespace cpp_chat::storage
