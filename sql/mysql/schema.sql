-- 创建聊天服务使用的数据库。
-- utf8mb4 支持完整 Unicode 字符集，后续聊天内容和日志可以保存中文、表情等字符。
CREATE DATABASE IF NOT EXISTS cpp_chat
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

-- 后续建表语句默认落在 cpp_chat 数据库中。
USE cpp_chat;

-- 用户表。
-- password_hash 保存 PBKDF2-HMAC-SHA256 的算法、迭代次数、盐和派生结果，不保存明文密码。
CREATE TABLE IF NOT EXISTS users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_users_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 服务端日志表。
-- Logger 启动时也会执行同等的 CREATE TABLE IF NOT EXISTS，这个脚本用于初始化环境。
CREATE TABLE IF NOT EXISTS server_logs (
    -- 自增主键，便于按插入顺序查看日志。
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,

    -- 日志创建时间，默认由 MySQL 写入当前时间。
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    -- 日志级别：INFO/WARN/ERROR。
    level VARCHAR(16) NOT NULL,

    -- 日志正文，保存服务启动、连接、协议解析、消息投递等事件。
    message TEXT NOT NULL,

    -- 按时间和级别查询是最常见的日志检索方式，因此单独建索引。
    INDEX idx_server_logs_created_at (created_at),
    INDEX idx_server_logs_level (level)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 聊天消息表。
-- MessageStore 启动时也会执行同等的 CREATE TABLE IF NOT EXISTS，这个脚本用于初始化环境。
CREATE TABLE IF NOT EXISTS messages (
    -- 自增主键，按插入顺序唯一标识每条消息。
    id                BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,

    -- 会话类型：1=私聊，2=群聊；配合 conversation_id 支持统一历史分页。
    conversation_type TINYINT         NOT NULL DEFAULT 0,

    -- 私聊为 min(user_id, peer_id) << 32 | max(user_id, peer_id)，群聊为 group_id。
    conversation_id   BIGINT UNSIGNED NOT NULL DEFAULT 0,

    -- 消息创建时间，默认由 MySQL 写入当前时间。
    created_at        TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,

    -- 消息类型：DirectChat / GroupChat / System。
    type              VARCHAR(32)     NOT NULL,

    -- 发送方业务用户 ID。
    sender_id         BIGINT UNSIGNED NOT NULL,

    -- 接收方业务用户 ID（私聊）或群组 ID（群聊）。
    receiver_id       BIGINT UNSIGNED NOT NULL,

    -- 消息正文，支持完整 Unicode 字符集。
    body              TEXT            NOT NULL,

    -- 按发送方、接收方和时间查询是最常见的检索方式，分别建索引。
    INDEX idx_conv_id_desc      (conversation_type, conversation_id, id),
    INDEX idx_sender_created    (sender_id, created_at),
    INDEX idx_messages_sender   (sender_id),
    INDEX idx_messages_receiver (receiver_id),
    INDEX idx_messages_created  (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 群资料表。
-- group_members 只保存成员关系，groups 保存群名称、群主和创建时间。
CREATE TABLE IF NOT EXISTS `groups` (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    owner_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_groups_owner (owner_id),

    CONSTRAINT fk_groups_owner
        FOREIGN KEY (owner_id) REFERENCES users(id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 群成员关系表。
-- 为兼容旧的直接 group_id 流程，group_id 暂不强制外键到 groups。
CREATE TABLE IF NOT EXISTS group_members (
    group_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    joined_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    -- 防止重复加群，并支持按群加载完整成员列表。
    PRIMARY KEY (group_id, user_id),

    -- 后续可用于查询某个用户加入的群列表。
    INDEX idx_group_members_user_group (user_id, group_id),

    -- 便于排查和统计加群时间。
    INDEX idx_group_members_joined_at (joined_at),

    -- 用户删除后自动清理成员关系；group_id 外键留到 groups 表落地后再加。
    CONSTRAINT fk_group_members_user
        FOREIGN KEY (user_id) REFERENCES users(id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
