CREATE USER IF NOT EXISTS 'cpp_chat'@'%' IDENTIFIED BY 'cpp_chat';
GRANT ALL PRIVILEGES ON cpp_chat.* TO 'cpp_chat'@'%';

CREATE USER IF NOT EXISTS 'cpp_chat'@'localhost' IDENTIFIED BY 'cpp_chat';
GRANT ALL PRIVILEGES ON cpp_chat.* TO 'cpp_chat'@'localhost';

FLUSH PRIVILEGES;
