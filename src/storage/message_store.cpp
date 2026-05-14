#include "cpp_chat/storage/message_store.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <mysql.h>
#include <set>
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

std::uint8_t conversation_type_for(protocol::MessageType type) {
    if (type == protocol::MessageType::DirectChat) {
        return 1;
    }
    if (type == protocol::MessageType::GroupChat) {
        return 2;
    }
    return 0;
}

std::uint64_t make_direct_conversation_id(std::uint64_t a, std::uint64_t b) {
    const auto low = std::min(a, b);
    const auto high = std::max(a, b);
    return (low << 32u) | high;
}

std::uint64_t conversation_id_for(const protocol::Message& message) {
    if (message.type == protocol::MessageType::DirectChat) {
        return make_direct_conversation_id(message.sender_id, message.receiver_id);
    }
    if (message.type == protocol::MessageType::GroupChat) {
        return message.receiver_id;
    }
    return 0;
}

// 对简单 SQL 执行一层包装，让构造函数的错误处理更清晰。
bool execute_query(MYSQL* mysql, const char* query) {
    return mysql_query(mysql, query) == 0;
}

bool table_column_exists(MYSQL* mysql, const char* table, const char* column) {
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        return false;
    }

    const char* query =
        "SELECT COUNT(*) "
        "FROM INFORMATION_SCHEMA.COLUMNS "
        "WHERE TABLE_SCHEMA = DATABASE() "
        "AND TABLE_NAME = ? "
        "AND COLUMN_NAME = ?";
    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        mysql_stmt_close(statement);
        return false;
    }

    MYSQL_BIND params[2]{};
    unsigned long table_length = static_cast<unsigned long>(std::strlen(table));
    unsigned long column_length = static_cast<unsigned long>(std::strlen(column));

    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = const_cast<char*>(table);
    params[0].buffer_length = table_length;
    params[0].length = &table_length;

    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = const_cast<char*>(column);
    params[1].buffer_length = column_length;
    params[1].length = &column_length;

    if (mysql_stmt_bind_param(statement, params) != 0 ||
        mysql_stmt_execute(statement) != 0) {
        mysql_stmt_close(statement);
        return false;
    }

    MYSQL_BIND result{};
    std::uint64_t count = 0;
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &count;
    result.is_unsigned = true;

    if (mysql_stmt_bind_result(statement, &result) != 0 ||
        mysql_stmt_fetch(statement) != 0) {
        mysql_stmt_close(statement);
        return false;
    }

    mysql_stmt_close(statement);
    return count > 0;
}

void ensure_messages_schema(MYSQL* mysql) {
    if (!table_column_exists(mysql, "messages", "conversation_type")) {
        execute_query(mysql,
            "ALTER TABLE messages "
            "ADD COLUMN conversation_type TINYINT NOT NULL DEFAULT 0 AFTER id");
    }
    if (!table_column_exists(mysql, "messages", "conversation_id")) {
        execute_query(mysql,
            "ALTER TABLE messages "
            "ADD COLUMN conversation_id BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER conversation_type");
    }
    execute_query(mysql,
        "CREATE INDEX idx_conv_id_desc ON messages (conversation_type, conversation_id, id)");
    execute_query(mysql,
        "CREATE INDEX idx_sender_created ON messages (sender_id, created_at)");
    execute_query(mysql,
        "UPDATE messages "
        "SET conversation_type = CASE "
        "    WHEN type = 'DirectChat' THEN 1 "
        "    WHEN type = 'GroupChat' THEN 2 "
        "    ELSE 0 END, "
        "conversation_id = CASE "
        "    WHEN type = 'DirectChat' THEN ((LEAST(sender_id, receiver_id) << 32) | GREATEST(sender_id, receiver_id)) "
        "    WHEN type = 'GroupChat' THEN receiver_id "
        "    ELSE 0 END "
        "WHERE conversation_type = 0");
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

std::vector<StoredMessage> execute_history_page_query(MYSQL* mysql,
                                                      const char* query,
                                                      protocol::MessageType type,
                                                      const std::vector<std::uint64_t>& id_params,
                                                      std::uint32_t limit) {
    std::vector<StoredMessage> messages;

    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        std::cerr << "[ERROR] MessageStore: mysql_stmt_init failed for paged history query" << std::endl;
        return messages;
    }

    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to prepare paged history query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return messages;
    }

    std::vector<MYSQL_BIND> params(id_params.size() + 1);
    for (std::size_t i = 0; i < id_params.size(); ++i) {
        params[i].buffer_type = MYSQL_TYPE_LONGLONG;
        params[i].buffer = const_cast<std::uint64_t*>(&id_params[i]);
        params[i].is_unsigned = true;
    }
    unsigned long limit_value = limit;
    params[id_params.size()].buffer_type = MYSQL_TYPE_LONG;
    params[id_params.size()].buffer = &limit_value;
    params[id_params.size()].is_unsigned = true;

    if (mysql_stmt_bind_param(statement, params.data()) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to bind paged history query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return messages;
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to execute paged history query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return messages;
    }

    MYSQL_BIND results[5]{};
    std::uint64_t id = 0;
    std::uint64_t sender_id = 0;
    std::uint64_t receiver_id = 0;
    char body_probe = '\0';
    unsigned long body_length = 0;
    bool body_is_null = false;
    char created_at[32]{};
    unsigned long created_at_length = 0;

    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].is_unsigned = true;

    results[1].buffer_type = MYSQL_TYPE_LONGLONG;
    results[1].buffer = &sender_id;
    results[1].is_unsigned = true;

    results[2].buffer_type = MYSQL_TYPE_LONGLONG;
    results[2].buffer = &receiver_id;
    results[2].is_unsigned = true;

    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = &body_probe;
    results[3].buffer_length = 0;
    results[3].length = &body_length;
    results[3].is_null = &body_is_null;

    results[4].buffer_type = MYSQL_TYPE_STRING;
    results[4].buffer = created_at;
    results[4].buffer_length = sizeof(created_at);
    results[4].length = &created_at_length;

    if (mysql_stmt_bind_result(statement, results) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to bind paged history result: "
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
            std::cerr << "[ERROR] MessageStore: failed to fetch paged history row: "
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

            if (mysql_stmt_fetch_column(statement, &body_result, 3, 0) != 0) {
                std::cerr << "[ERROR] MessageStore: failed to fetch paged history body: "
                          << mysql_stmt_error(statement) << std::endl;
                continue;
            }
        }

        messages.push_back({id, type, sender_id, receiver_id, body,
                            std::string(created_at, created_at_length)});
    }

    mysql_stmt_close(statement);
    return messages;
}

std::vector<StoredMessage> execute_unread_query(MYSQL* mysql,
                                                std::uint64_t user_id,
                                                const std::vector<std::uint64_t>& group_ids,
                                                std::uint64_t last_seen_message_id,
                                                std::uint32_t limit) {
    std::string query =
        "SELECT id, type, sender_id, receiver_id, body, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM messages "
        "WHERE id > ? AND ("
        "(conversation_type = 1 AND receiver_id = ?)";
    for (std::size_t i = 0; i < group_ids.size(); ++i) {
        query += " OR (conversation_type = 2 AND conversation_id = ? AND sender_id <> ?)";
    }
    query += ") ORDER BY id ASC LIMIT ?";

    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        std::cerr << "[ERROR] MessageStore: mysql_stmt_init failed for unread query" << std::endl;
        return {};
    }

    if (mysql_stmt_prepare(statement, query.c_str(), static_cast<unsigned long>(query.size())) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to prepare unread query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {};
    }

    std::vector<std::uint64_t> params_storage;
    params_storage.reserve(2 + group_ids.size() * 2 + 1);
    params_storage.push_back(last_seen_message_id);
    params_storage.push_back(user_id);
    for (const auto group_id : group_ids) {
        params_storage.push_back(group_id);
        params_storage.push_back(user_id);
    }
    params_storage.push_back(limit);

    std::vector<MYSQL_BIND> params(params_storage.size());
    for (std::size_t i = 0; i < params_storage.size(); ++i) {
        params[i].buffer_type = MYSQL_TYPE_LONGLONG;
        params[i].buffer = &params_storage[i];
        params[i].is_unsigned = true;
    }

    if (mysql_stmt_bind_param(statement, params.data()) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to bind unread query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {};
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to execute unread query: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {};
    }

    std::vector<StoredMessage> messages;
    MYSQL_BIND results[6]{};
    std::uint64_t id = 0;
    char type_buffer[32]{};
    unsigned long type_length = 0;
    std::uint64_t sender_id = 0;
    std::uint64_t receiver_id = 0;
    char body_probe = '\0';
    unsigned long body_length = 0;
    bool body_is_null = false;
    char created_at[32]{};
    unsigned long created_at_length = 0;

    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].is_unsigned = true;

    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = type_buffer;
    results[1].buffer_length = sizeof(type_buffer);
    results[1].length = &type_length;

    results[2].buffer_type = MYSQL_TYPE_LONGLONG;
    results[2].buffer = &sender_id;
    results[2].is_unsigned = true;

    results[3].buffer_type = MYSQL_TYPE_LONGLONG;
    results[3].buffer = &receiver_id;
    results[3].is_unsigned = true;

    results[4].buffer_type = MYSQL_TYPE_STRING;
    results[4].buffer = &body_probe;
    results[4].buffer_length = 0;
    results[4].length = &body_length;
    results[4].is_null = &body_is_null;

    results[5].buffer_type = MYSQL_TYPE_STRING;
    results[5].buffer = created_at;
    results[5].buffer_length = sizeof(created_at);
    results[5].length = &created_at_length;

    if (mysql_stmt_bind_result(statement, results) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to bind unread result: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return {};
    }

    while (true) {
        const int fetch_status = mysql_stmt_fetch(statement);
        if (fetch_status == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
            std::cerr << "[ERROR] MessageStore: failed to fetch unread row: "
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

            if (mysql_stmt_fetch_column(statement, &body_result, 4, 0) != 0) {
                std::cerr << "[ERROR] MessageStore: failed to fetch unread body: "
                          << mysql_stmt_error(statement) << std::endl;
                continue;
            }
        }

        const std::string type(type_buffer, type_length);
        const auto message_type = type == "GroupChat"
            ? protocol::MessageType::GroupChat
            : protocol::MessageType::DirectChat;
        messages.push_back({id, message_type, sender_id, receiver_id, body,
                            std::string(created_at, created_at_length)});
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
        "id                BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "conversation_type TINYINT         NOT NULL DEFAULT 0,"
        "conversation_id   BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "created_at        TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "type              VARCHAR(32)     NOT NULL,"
        "sender_id         BIGINT UNSIGNED NOT NULL,"
        "receiver_id       BIGINT UNSIGNED NOT NULL,"
        "body              TEXT            NOT NULL,"
        "INDEX idx_conv_id_desc     (conversation_type, conversation_id, id),"
        "INDEX idx_sender_created   (sender_id, created_at),"
        "INDEX idx_messages_sender  (sender_id),"
        "INDEX idx_messages_receiver(receiver_id),"
        "INDEX idx_messages_created (created_at)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    if (!execute_query(mysql, create_table)) {
        std::cerr << "[ERROR] MessageStore: failed to create messages table: "
                  << mysql_error(mysql) << std::endl;
    }
    ensure_messages_schema(mysql);
}

StoredMessage MessageStore::append(protocol::Message message) {
    StoredMessage stored;
    stored.type = message.type;
    stored.sender_id = message.sender_id;
    stored.receiver_id = message.receiver_id;
    stored.body = message.body;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 先写内存，保证当次运行期间 all() 可以快速返回。
        messages_.push_back(message);
        stored.id = messages_.size();
    }

    // 数据库不可用时退化为内存模式，不影响主业务流程。
    if (pool_ == nullptr) {
        return stored;
    }

    auto connection = pool_->acquire();
    if (!connection) {
        return stored;
    }
    MYSQL* mysql = connection.get();

    // 使用预处理语句写消息，防止 body 中包含特殊字符时破坏 SQL。
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (statement == nullptr) {
        std::cerr << "[ERROR] MessageStore: mysql_stmt_init failed" << std::endl;
        return stored;
    }

    const char* query =
        "INSERT INTO messages "
        "(conversation_type, conversation_id, type, sender_id, receiver_id, body) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    if (mysql_stmt_prepare(statement, query, static_cast<unsigned long>(std::strlen(query))) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to prepare insert: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return stored;
    }

    const char* type_str = message_type_to_string(message.type);
    const auto conversation_type = conversation_type_for(message.type);
    const auto conversation_id = conversation_id_for(message);
    unsigned long type_length = static_cast<unsigned long>(std::strlen(type_str));
    unsigned long body_length = static_cast<unsigned long>(message.body.size());

    MYSQL_BIND bind[6]{};

    bind[0].buffer_type = MYSQL_TYPE_TINY;
    bind[0].buffer = const_cast<std::uint8_t*>(&conversation_type);
    bind[0].is_unsigned = true;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = const_cast<std::uint64_t*>(&conversation_id);
    bind[1].is_unsigned = true;

    // type: 消息类型字符串（DirectChat / GroupChat / System）。
    bind[2].buffer_type   = MYSQL_TYPE_STRING;
    bind[2].buffer        = const_cast<char*>(type_str);
    bind[2].buffer_length = type_length;
    bind[2].length        = &type_length;

    // sender_id: 发送方业务用户 ID。
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer      = &message.sender_id;
    bind[3].is_unsigned = true;

    // receiver_id: 接收方业务用户 ID（私聊）或群组 ID（群聊）。
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer      = &message.receiver_id;
    bind[4].is_unsigned = true;

    // body: 消息正文，支持任意 UTF-8 文本。
    bind[5].buffer_type   = MYSQL_TYPE_STRING;
    bind[5].buffer        = const_cast<char*>(message.body.data());
    bind[5].buffer_length = body_length;
    bind[5].length        = &body_length;

    if (mysql_stmt_bind_param(statement, bind) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to bind insert: "
                  << mysql_stmt_error(statement) << std::endl;
        mysql_stmt_close(statement);
        return stored;
    }

    if (mysql_stmt_execute(statement) != 0) {
        std::cerr << "[ERROR] MessageStore: failed to execute insert: "
                  << mysql_stmt_error(statement) << std::endl;
    } else {
        stored.id = mysql_insert_id(mysql);
    }

    mysql_stmt_close(statement);
    return stored;
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

std::vector<StoredMessage> MessageStore::direct_history_page(std::uint64_t user_id,
                                                             std::uint64_t peer_id,
                                                             std::uint32_t limit,
                                                             std::uint64_t before_id) const {
    if (limit == 0) {
        limit = 20;
    }

    if (pool_ != nullptr) {
        auto connection = pool_->acquire();
        if (connection) {
            MYSQL* mysql = connection.get();
            const auto conversation_id = make_direct_conversation_id(user_id, peer_id);

            if (before_id > 0) {
                const char* query =
                    "SELECT id, sender_id, receiver_id, body, "
                    "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
                    "FROM messages "
                    "WHERE conversation_type = 1 AND conversation_id = ? AND id < ? "
                    "ORDER BY id DESC "
                    "LIMIT ?";
                return execute_history_page_query(
                    mysql, query, protocol::MessageType::DirectChat,
                    {conversation_id, before_id}, limit);
            }

            const char* query =
                "SELECT id, sender_id, receiver_id, body, "
                "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
                "FROM messages "
                "WHERE conversation_type = 1 AND conversation_id = ? "
                "ORDER BY id DESC "
                "LIMIT ?";
            return execute_history_page_query(
                mysql, query, protocol::MessageType::DirectChat,
                {conversation_id}, limit);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredMessage> result;
    for (std::size_t index = messages_.size(); index > 0 && result.size() < limit; --index) {
        const auto& msg = messages_[index - 1];
        const auto message_id = static_cast<std::uint64_t>(index);
        if (before_id > 0 && message_id >= before_id) {
            continue;
        }
        if (msg.type == protocol::MessageType::DirectChat &&
            ((msg.sender_id == user_id && msg.receiver_id == peer_id) ||
             (msg.sender_id == peer_id && msg.receiver_id == user_id))) {
            result.push_back({message_id, msg.type, msg.sender_id, msg.receiver_id, msg.body, ""});
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

std::vector<StoredMessage> MessageStore::group_history_page(std::uint64_t group_id,
                                                            std::uint32_t limit,
                                                            std::uint64_t before_id) const {
    if (limit == 0) {
        limit = 20;
    }

    if (pool_ != nullptr) {
        auto connection = pool_->acquire();
        if (connection) {
            MYSQL* mysql = connection.get();

            if (before_id > 0) {
                const char* query =
                    "SELECT id, sender_id, receiver_id, body, "
                    "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
                    "FROM messages "
                    "WHERE conversation_type = 2 AND conversation_id = ? AND id < ? "
                    "ORDER BY id DESC "
                    "LIMIT ?";
                return execute_history_page_query(
                    mysql, query, protocol::MessageType::GroupChat,
                    {group_id, before_id}, limit);
            }

            const char* query =
                "SELECT id, sender_id, receiver_id, body, "
                "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
                "FROM messages "
                "WHERE conversation_type = 2 AND conversation_id = ? "
                "ORDER BY id DESC "
                "LIMIT ?";
            return execute_history_page_query(
                mysql, query, protocol::MessageType::GroupChat,
                {group_id}, limit);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredMessage> result;
    for (std::size_t index = messages_.size(); index > 0 && result.size() < limit; --index) {
        const auto& msg = messages_[index - 1];
        const auto message_id = static_cast<std::uint64_t>(index);
        if (before_id > 0 && message_id >= before_id) {
            continue;
        }
        if (msg.type == protocol::MessageType::GroupChat && msg.receiver_id == group_id) {
            result.push_back({message_id, msg.type, msg.sender_id, msg.receiver_id, msg.body, ""});
        }
    }
    return result;
}

std::vector<StoredMessage> MessageStore::unread_page(
    std::uint64_t user_id,
    const std::vector<std::uint64_t>& group_ids,
    std::uint64_t last_seen_message_id,
    std::uint32_t limit) const {
    if (limit == 0) {
        limit = 20;
    }

    if (pool_ != nullptr) {
        auto connection = pool_->acquire();
        if (connection) {
            return execute_unread_query(connection.get(), user_id, group_ids, last_seen_message_id, limit);
        }
    }

    std::set<std::uint64_t> group_id_set(group_ids.begin(), group_ids.end());
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredMessage> result;
    for (std::size_t index = 0; index < messages_.size() && result.size() < limit; ++index) {
        const auto message_id = static_cast<std::uint64_t>(index + 1);
        if (message_id <= last_seen_message_id) {
            continue;
        }

        const auto& msg = messages_[index];
        const bool direct_unread = msg.type == protocol::MessageType::DirectChat &&
                                   msg.receiver_id == user_id;
        const bool group_unread = msg.type == protocol::MessageType::GroupChat &&
                                  msg.sender_id != user_id &&
                                  group_id_set.count(msg.receiver_id) > 0;
        if (direct_unread || group_unread) {
            result.push_back({message_id, msg.type, msg.sender_id, msg.receiver_id, msg.body, ""});
        }
    }
    return result;
}

} // namespace cpp_chat::storage
