CREATE DATABASE IF NOT EXISTS cpp_chat
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE cpp_chat;

CREATE TABLE IF NOT EXISTS server_logs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    level VARCHAR(16) NOT NULL,
    message TEXT NOT NULL,
    INDEX idx_server_logs_created_at (created_at),
    INDEX idx_server_logs_level (level)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
