-- 创建容器或远程客户端访问用账号。
-- '%' 表示允许从任意主机连接，适合 docker-compose 网络内访问。
CREATE USER IF NOT EXISTS 'cpp_chat'@'%' IDENTIFIED BY 'cpp_chat';

-- 授权该账号访问 cpp_chat 数据库下所有对象。
GRANT ALL PRIVILEGES ON cpp_chat.* TO 'cpp_chat'@'%';

-- 创建本机访问用账号，方便在宿主机直接运行服务或调试。
CREATE USER IF NOT EXISTS 'cpp_chat'@'localhost' IDENTIFIED BY 'cpp_chat';

-- localhost 账号同样只授权 cpp_chat 数据库，避免扩大权限范围。
GRANT ALL PRIVILEGES ON cpp_chat.* TO 'cpp_chat'@'localhost';

-- 立即刷新权限，使上面的 CREATE USER/GRANT 生效。
FLUSH PRIVILEGES;
