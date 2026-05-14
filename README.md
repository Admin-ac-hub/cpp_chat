# cpp_chat

[![CI](https://github.com/Admin-ac-hub/cpp_chat/actions/workflows/ci.yml/badge.svg)](https://github.com/Admin-ac-hub/cpp_chat/actions/workflows/ci.yml)

`cpp_chat` 是一个基于 C++17、Linux `epoll` 和 MySQL 的 IM/chat 服务端，重点展示后端工程里的网络 IO、协议拆包、会话管理、消息持久化、连接池、测试和 CI 能力。

## 30 秒看懂

- **网络模型**：非阻塞 socket + `epoll` Reactor，支持多 TCP 长连接和连接生命周期管理。
- **私有协议**：`4 字节网络序长度 + JSON payload`，通过 `PacketCodec` 处理粘包、半包、空包和超大包。
- **核心业务**：注册登录、重复登录拒绝、断开清理、在线私聊、离线消息、群聊投递、群成员校验。
- **消息存储**：MySQL 持久化用户、消息、群关系和日志，历史消息使用 `message_id` 游标分页。
- **稳定性设计**：MySQL 连接池支持 acquire/release、健康检查、异常重连；应用层有读写缓冲、响应队列和线程池背压限制。
- **工程化交付**：Docker Compose 一键启动，GoogleTest 单元测试，Python 端到端集成测试，GitHub Actions 自动构建和验证。

默认服务端监听 `0.0.0.0:9000`。

## 一键启动

```bash
docker compose -f docker/docker-compose.yml up --build
```

另开终端执行端到端验收：

```bash
python3 scripts/integration_test.py --host 127.0.0.1 --port 9000
python3 scripts/load_test.py --host 127.0.0.1 --port 9000
```

Linux 本地构建与测试：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

> 项目依赖 Linux `epoll`，macOS 本机不适合直接完整编译服务端；推荐使用 Docker 或 GitHub Actions 验证。

## 简历可写亮点

```text
基于 C++17 实现 Linux epoll 长连接 IM 服务端，设计 length-prefix JSON 私有协议解决 TCP 粘包/半包问题；
支持注册登录、重复登录拦截、私聊/群聊、离线消息持久化和 message_id 游标分页历史查询；
实现 MySQL 连接池、空闲健康检查、异常重连和应用层背压控制，并通过 GoogleTest、Python 集成测试和 GitHub Actions 建立自动化验证链路。
```

## 面试演示路径

```text
启动服务 -> 注册用户 -> 登录 -> 单聊 -> 离线消息持久化 -> 历史记录分页查询 -> 压测
```

## 1. 项目简介

`cpp_chat` 实现了一个单机 IM 服务端，采用网络层、协议层、业务层、会话层、存储层分层设计：

- 网络层使用非阻塞 socket + `epoll` 处理多连接 IO。
- 协议层使用 length-prefix 帧解决 TCP 粘包、半包和消息边界问题。
- 业务层负责登录校验、消息路由、群聊权限和历史查询。
- 存储层使用 MySQL 保存用户、消息、群成员关系和服务端日志。
- 测试与脚本覆盖 GoogleTest 单元测试、Python 端到端集成测试和 TCP 基准压测。

项目定位是一个可运行、可测试、可部署、可讲解的 C++ 后端面试项目。

## 2. 核心能力

- **TCP 长连接服务**：基于 Reactor + `epoll` 管理客户端连接、读写事件和连接生命周期。
- **自定义应用层协议**：使用 4 字节 length-prefix 切分 TCP 字节流，处理粘包、半包、空包和超大包。
- **用户认证**：支持注册、登录、重复登录拦截和断开连接后的会话清理。
- **消息通信**：支持在线私聊、离线消息持久化、群成员校验和群聊投递。
- **历史查询**：使用 `message_id` 游标分页，避免 `OFFSET` 深分页性能退化。
- **MySQL 持久化**：保存用户、消息、群成员关系和服务日志。
- **连接池**：复用 MySQL 连接，支持 acquire 超时、空闲连接健康检查和异常重连。
- **连接生命周期管理**：fd 只作为系统句柄，业务层使用服务端自增 `connection_id`，避免 fd 复用误投递。
- **应用层背压控制**：限制读缓冲、写缓冲、响应队列和线程池队列，超限时记录日志并断开连接。
- **工程化交付**：提供 Docker Compose 一键启动、GoogleTest、集成测试和压测脚本。

## 3. 技术栈

- C++17
- Linux `epoll`
- TCP 长连接
- Length-prefix JSON 协议
- MySQL / MariaDB
- OpenSSL PBKDF2-HMAC-SHA256
- CMake
- Docker Compose
- GoogleTest
- Python 集成测试与压测脚本

## 4. 架构图

```text
Client
  |
  | 4-byte length + JSON payload over TCP
  v
TcpServer
  |
  | epoll / non-blocking socket / read-write buffers
  v
PacketCodec
  |
  | decoded JSON payload
  v
ChatService
  |---- SessionManager
  |---- UserStore
  |---- MessageStore
  |---- GroupMemberStore
  |---- Logger
  v
MySQL
```

模块职责：

- `network`：监听端口、接收连接、非阻塞读写、`epoll` 事件循环、连接缓冲区和心跳超时。
- `protocol`：解析客户端 JSON 命令，格式化服务端 JSON 响应。
- `chat`：编排注册登录、私聊、群聊、历史查询和权限校验。
- `session`：维护在线用户与连接的映射关系。
- `storage`：持久化用户、消息、群成员关系和服务端日志。
- `core`：配置加载、线程池等基础设施。

## 5. 快速启动

推荐使用 Docker Compose 启动服务端和 MySQL：

```bash
docker compose -f docker/docker-compose.yml up --build
```

该命令会：

- 构建 C++ 服务端镜像。
- 在镜像构建阶段编译服务端和测试目标。
- 运行 GoogleTest 单元测试。
- 启动 MySQL 8.4 容器并初始化表结构。
- 启动 `cpp_chat_server` 并暴露本机 `9000` 端口。

服务启动后，在另一个终端执行验收命令：

```bash
python3 scripts/integration_test.py --host 127.0.0.1 --port 9000
python3 scripts/load_test.py --host 127.0.0.1 --port 9000
```

本地构建方式：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/cpp_chat_server
```

常用运行配置可以通过 `config/mysql.env` 或环境变量覆盖。背压相关配置：

```text
CPP_CHAT_MAX_READ_BUFFER_BYTES=2097152
CPP_CHAT_MAX_WRITE_BUFFER_BYTES=2097152
CPP_CHAT_MAX_RESPONSE_QUEUE_SIZE=10000
CPP_CHAT_THREAD_POOL_MAX_QUEUE_SIZE=10000
```

这些限制用于防止慢客户端或突发请求导致服务端内存无限增长。超过限制时，服务端会记录日志、清理会话并主动断开对应连接。

## 6. 协议说明

每个请求和响应都是一个二进制帧：

```text
0               4                         4 + length
+---------------+-------------------------+
| uint32 length | JSON payload bytes      |
+---------------+-------------------------+
```

- `length` 为 4 字节无符号整数，使用网络字节序。
- `length` 表示 JSON payload 字节数，不包含自身 4 字节。
- payload 使用 UTF-8 JSON。
- 空 payload 和超过 1 MiB 的 payload 会被拒绝。

### 请求示例

注册：

```json
{"type":"register","username":"alice","password":"secret"}
```

登录：

```json
{"type":"login","username":"alice","password":"secret"}
```

私聊：

```json
{"type":"dm","to":"bob","body":"hello"}
```

创建群：

```json
{"type":"create_group","name":"backend"}
```

加入群：

```json
{"type":"join_group","group_id":100}
```

群聊：

```json
{"type":"group_message","group_id":100,"body":"hello group"}
```

私聊历史：

```json
{"type":"history","peer":"bob","limit":20,"before_id":123}
```

群聊历史：

```json
{"type":"group_history","group_id":100,"limit":20,"before_id":123}
```

未读拉取：

```json
{"type":"unread","last_seen_message_id":123,"limit":20}
```

心跳：

```json
{"type":"ping"}
```

### 响应示例

通用成功：

```json
{"type":"ok","message":"sent"}
```

通用错误：

```json
{"type":"error","reason":"please login first"}
```

登录成功：

```json
{"type":"login_success","user_id":1,"username":"alice"}
```

建群成功：

```json
{"type":"create_group_success","group_id":100,"name":"backend"}
```

消息 ACK：

```json
{"type":"message_ack","message_id":123,"status":"stored","stored":true,"delivered":false}
```

私聊投递：

```json
{"type":"dm","from":"alice","body":"hello"}
```

历史消息：

```json
{
  "type": "history_item",
  "message_id": 122,
  "chat_type": "dm",
  "from": "alice",
  "to": "bob",
  "body": "hello",
  "created_at": "2026-05-13 12:00:00"
}
```

历史结束：

```json
{"type":"history_end","has_more":true,"next_before_id":103}
```

未读结束：

```json
{"type":"unread_end","has_more":false,"next_last_seen_message_id":123}
```

心跳响应：

```json
{"type":"pong"}
```

## 7. 数据库设计

核心表包括 `users`、`messages`、`groups`、`group_members` 和 `server_logs`。

```sql
CREATE TABLE users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_users_username (username)
);

CREATE TABLE messages (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    conversation_type TINYINT NOT NULL DEFAULT 0,
    conversation_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    type VARCHAR(32) NOT NULL,
    sender_id BIGINT UNSIGNED NOT NULL,
    receiver_id BIGINT UNSIGNED NOT NULL,
    body TEXT NOT NULL,
    INDEX idx_conv_id_desc (conversation_type, conversation_id, id),
    INDEX idx_sender_created (sender_id, created_at)
);

CREATE TABLE `groups` (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    owner_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_groups_owner (owner_id)
);

CREATE TABLE group_members (
    group_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    joined_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (group_id, user_id),
    INDEX idx_group_members_user_group (user_id, group_id)
);

CREATE TABLE server_logs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    level VARCHAR(16) NOT NULL,
    message TEXT NOT NULL,
    INDEX idx_server_logs_created_at (created_at),
    INDEX idx_server_logs_level (level)
);
```

关键设计：

- `messages.id` 是全局自增主键，对外作为 `message_id` 返回，也作为历史分页游标。
- `conversation_type` 区分私聊和群聊，`conversation_id` 标识具体会话。
- 私聊会话 ID 计算方式为 `min(user_id, peer_id) << 32 | max(user_id, peer_id)`，保证双方消息落在同一会话。
- `(conversation_type, conversation_id, id)` 索引用于历史消息分页查询。
- `groups` 保存群名称、群主和创建时间；`group_members` 保存群成员关系。
- `group_members` 使用 `(group_id, user_id)` 主键避免重复加群。

历史分页查询使用游标条件：

```sql
SELECT id, sender_id, receiver_id, body, created_at
FROM messages
WHERE conversation_type = ?
  AND conversation_id = ?
  AND id < ?
ORDER BY id DESC
LIMIT ?;
```

相比 `OFFSET` 分页，`id < before_id` 能减少深分页扫描成本，也能避免新消息插入导致的重复或漏读。

## 8. 核心流程

### 注册与登录

1. 客户端发送 `register` 或 `login` 请求。
2. 服务端解析 length-prefix 帧并校验 JSON 字段。
3. 注册时写入用户表，密码使用 PBKDF2-HMAC-SHA256 哈希保存。
4. 登录时校验密码，成功后绑定 `connection_id -> user_id/username`。
5. 同一用户名已在线时拒绝重复登录。

### 私聊

1. 客户端发送 `dm` 请求。
2. 服务端校验发送方登录态。
3. 查询接收方用户是否存在。
4. 消息写入 MySQL `messages` 表。
5. 服务端向发送方返回 `message_ack`，其中 `message_id` 表示消息已入库。
6. 接收方在线时实时投递并返回 `delivered:true`；接收方离线时返回 `delivered:false`，但仍表示消息已持久化。

### 群聊

1. 用户发送 `create_group` 创建群，服务端写入 `groups` 并自动把创建者加入 `group_members`。
2. 用户也可以发送 `join_group` 加入已有或兼容旧流程的数字群 ID。
3. 发送 `group_message` 前校验发送者是否属于该群。
4. 消息写入 MySQL，发送方收到带 `message_id` 的 `message_ack`。
5. 服务端加载群成员，将消息投递给当前在线的其他成员。

### 历史查询

1. 客户端发送 `history` 或 `group_history`。
2. 服务端校验登录态、peer 是否存在或群成员权限。
3. 根据私聊用户对或群号计算会话标识。
4. 使用 `message_id` 游标分页读取消息。
5. 服务端按时间正序返回多条 `history_item`，最后返回 `history_end`。

### 未读拉取

1. 客户端保存本地已读到的最大 `message_id`。
2. 客户端发送 `unread`，携带 `last_seen_message_id`。
3. 服务端返回 id 更大的私聊收件消息，以及用户所在群的新群消息。
4. 服务端不维护已读状态，最后返回 `unread_end` 和 `next_last_seen_message_id`。

## 9. 测试方式

单元测试：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

端到端集成测试：

```bash
python3 scripts/integration_test.py --host 127.0.0.1 --port 9000
```

集成测试覆盖：

- 注册两个用户。
- 两个用户分别登录。
- 用户 A 向用户 B 发送私聊。
- 用户 B 接收实时投递。
- 用户 B 查询与用户 A 的历史消息。

网络层基准压测：

```bash
python3 scripts/load_test.py --host 127.0.0.1 --port 9000
```

可选参数：

```bash
python3 scripts/load_test.py \
  --host 127.0.0.1 \
  --port 9000 \
  --connections 100 \
  --messages 1000 \
  --warmup 100
```

当前 `load_test.py` 使用 `ping/pong` 请求压测网络层基准吞吐和延迟，不包含注册、登录、消息写库等完整业务链路。

## 10. 压测结果

压测命令：

```bash
python3 scripts/load_test.py --host 127.0.0.1 --port 9000 --connections 100 --messages 1000 --warmup 100
```

记录模板：

```text
environment: local Linux / Docker MySQL
cpu: fill after benchmark
memory: fill after benchmark
mysql: Docker Compose mysql:8.4
connections: 100
messages: 1000
warmup: 100
success: fill after benchmark
failed: fill after benchmark
avg latency: fill after benchmark
p50 latency: fill after benchmark
p95 latency: fill after benchmark
p99 latency: fill after benchmark
qps: fill after benchmark
```

说明：

- 当前压测脚本用于评估 length-prefix TCP 服务的 ping/pong 基准性能。
- 完整聊天业务压测需要覆盖注册、登录、私聊、写 MySQL、历史查询，应单独扩展脚本后记录。
- 简历或面试展示时建议使用固定机器重新压测，并填入真实环境与真实数据。

## 11. 项目难点

- **TCP 字节流无消息边界**：需要通过 length-prefix 协议处理粘包、半包、空包和异常大包。
- **非阻塞 IO 状态管理**：读写都可能只完成一部分，需要维护连接级读缓冲和写缓冲。
- **fd 复用风险**：socket fd 关闭后可能被系统复用，需要用稳定 `connection_id` 区分业务连接身份。
- **业务处理与网络 IO 解耦**：网络层负责收发字节，业务层负责命令处理和响应生成。
- **应用层背压控制**：`epoll` 只解决 IO 就绪通知，不能自动防止慢连接和任务堆积，需要限制缓冲区和队列。
- **在线投递与离线持久化统一**：消息先持久化，再根据接收方在线状态决定是否实时投递。
- **历史消息深分页**：聊天记录持续增长，使用 `message_id` 游标替代 `OFFSET`。
- **数据库连接复用**：高并发请求下复用 MySQL 连接，避免频繁建连影响吞吐。
- **连接断开清理**：客户端异常断开或心跳超时后，需要及时释放 fd、缓冲区和登录会话。

## 12. 可继续优化方向

- 使用 Protobuf 替代 JSON，降低协议体积和解析成本。
- 增加消息 ACK、客户端重试和消息去重机制。
- 增加离线未读拉取、已读回执和未读计数。
- 引入 Redis 保存在线状态、热点会话和未读计数。
- 支持多 Reactor 线程模型，提高多核利用率。
- 引入消息队列，解耦写库、在线投递和离线通知。
- 增加 JWT/token 鉴权，支持断线重连和多端登录策略。
- 为群聊增加群主转让、管理员和权限模型。
- 增加 GitHub Actions CI，自动执行构建、单测和集成测试。

## 13. 简历描述

项目描述：

```text
基于 C++17 实现 Linux 高并发 IM/chat 服务端，采用 Reactor + epoll 非阻塞 IO 模型管理 TCP 长连接，通过 4 字节 length-prefix 私有协议解决 TCP 粘包和半包问题。项目支持用户注册登录、私聊、群聊、消息 ACK、离线消息持久化、未读消息拉取和历史消息游标分页查询，并基于 MySQL 连接池降低频繁建连开销。网络层使用自增 connection_id 隔离 socket fd 复用风险，并通过读写缓冲、响应队列和线程池队列上限实现应用层背压控制。提供 Docker Compose 一键部署、GoogleTest 单元测试、Python 端到端集成测试和 TCP 基准压测脚本。
```

项目亮点：

```text
- 使用 epoll + Reactor 实现 TCP 长连接服务，网络层与业务层解耦。
- 自定义 length-prefix 协议，完整处理粘包、半包、空包和超大包。
- 使用稳定 connection_id 管理连接生命周期，避免 fd 复用导致异步响应误投递。
- 增加读写缓冲、响应队列和线程池队列上限，防止慢连接拖垮服务。
- 私聊和群聊返回带 message_id 的 ACK，离线消息返回 stored=true、delivered=false。
- 基于 last_seen_message_id 实现未读消息拉取，不额外维护服务端已读状态。
- 使用 message_id 游标分页替代 OFFSET，优化聊天历史深分页查询。
- 设计 MySQL 连接池，支持连接复用、acquire 超时、健康检查和异常重连。
- 支持私聊、群聊、建群、离线持久化、未读拉取和历史记录查询，覆盖 IM 服务端核心链路。
- 提供 Docker Compose、GoogleTest、集成测试和压测脚本，保证项目可复现。
```
