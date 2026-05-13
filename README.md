# cpp_chat

A Linux TCP chat backend in C++17.

The server uses epoll-based non-blocking I/O, a worker thread pool, JSON-over-TCP
commands, MySQL-backed users/messages/logs, and GoogleTest coverage for the core
modules.

## Current Status

Implemented:

- epoll TCP server on Linux.
- newline-delimited JSON protocol.
- user registration and login.
- PBKDF2-HMAC-SHA256 password hashing with per-user salt.
- online session binding by `fd -> user_id/username`.
- duplicate-login rejection for already-online usernames.
- logout and disconnect cleanup.
- one-to-one chat by username.
- group join, leave, broadcast, and group history.
- MySQL persistence for users, messages, and server logs.
- MySQL-backed direct/group history queries.
- Docker Compose stack for server + MySQL.
- GoogleTest tests for protocol, session, storage, auth, and chat service.

Important limitations:

- The server targets Linux because the network layer uses `epoll`.
- The JSON parser is intentionally small and supports only the flat request
  objects used by this project.
- Group membership is still in memory and is not restored after restart.
- There is no token/session-cookie layer yet; login binds identity to the TCP
  connection.

## Project Layout

```text
.
├── CMakeLists.txt
├── include/cpp_chat/  # Public module headers
├── src/               # Module implementations
├── tests/             # GoogleTest unit tests
├── config/            # Runtime env-style config
├── sql/mysql/         # MySQL schema and user initialization
├── docker/            # Dockerfile and compose stack
├── logs/              # Local runtime logs, ignored by git
└── docs/              # Architecture notes
```

## Architecture

```text
main
  -> app
      -> network
      -> chat
          -> session
          -> storage
          -> protocol
      -> logging
      -> core
```

Module responsibilities:

- `network`: socket setup, epoll loop, buffering, response draining, heartbeat timeout.
- `protocol`: JSON command parsing and JSON response formatting.
- `session`: online users, connection binding, username lookup, group membership.
- `chat`: register/login/logout, delivery decisions, auth gates, history routing.
- `storage`: users, password hashes, messages, and history queries.
- `logging`: stdout + MySQL server logs.
- `core`: config loading and thread pool.

## Build Locally

Prerequisites:

- Linux environment.
- CMake 3.16+.
- C++17 compiler.
- MySQL or MariaDB client development library.
- OpenSSL development library.
- GoogleTest, optional. If it is missing, the test target is skipped.

Build:

```sh
cmake -S . -B build
cmake --build build
```

Run tests when `cpp_chat_tests` is available:

```sh
ctest --test-dir build --output-on-failure
```

Run the server:

```sh
./build/cpp_chat_server
```

By default the server listens on `0.0.0.0:9000`.

## Run With Docker

```sh
docker compose -f docker/docker-compose.yml up --build
```

Connect from another terminal:

```sh
nc 127.0.0.1 9000
```

Check users/messages/logs:

```sh
docker compose -f docker/docker-compose.yml exec mysql mysql -ucpp_chat -pcpp_chat cpp_chat \
  -e "SELECT id, username, password_hash, created_at FROM users ORDER BY id DESC LIMIT 5;"

docker compose -f docker/docker-compose.yml exec mysql mysql -ucpp_chat -pcpp_chat cpp_chat \
  -e "SELECT id, type, sender_id, receiver_id, body FROM messages ORDER BY id DESC LIMIT 10;"

docker compose -f docker/docker-compose.yml exec mysql mysql -ucpp_chat -pcpp_chat cpp_chat \
  -e "SELECT id, created_at, level, message FROM server_logs ORDER BY id DESC LIMIT 10;"
```

## Configuration

Default config file:

```text
config/mysql.env
```

Supported values:

```text
CPP_CHAT_LOG_DB_HOST=127.0.0.1
CPP_CHAT_LOG_DB_PORT=3306
CPP_CHAT_LOG_DB_USER=cpp_chat
CPP_CHAT_LOG_DB_PASSWORD=cpp_chat
CPP_CHAT_LOG_DB_NAME=cpp_chat
CPP_CHAT_DB_POOL_SIZE=4
```

`CPP_CHAT_DB_*` is also accepted for the shared database settings; the older
`CPP_CHAT_LOG_DB_*` names remain supported for compatibility.

The server merges configuration in this order:

1. built-in defaults;
2. `config/mysql.env`;
3. environment variables.

To initialize a local database manually:

```sh
mysql -u root -p < sql/mysql/schema.sql
mysql -u root -p < sql/mysql/create_user.sql
```

## JSON Protocol

Each request is one JSON object followed by `\n`. Each response is also one JSON
object followed by `\n`.

### Register

Request:

```json
{"type":"register","username":"alice","password":"secret"}
```

Success:

```json
{"type":"register_success","user_id":1}
```

Failure:

```json
{"type":"register_failed","reason":"username already exists"}
```

### Login

Request:

```json
{"type":"login","username":"alice","password":"secret"}
```

Success:

```json
{"type":"login_success","user_id":1,"username":"alice"}
```

Failure:

```json
{"type":"login_failed","reason":"invalid username or password"}
```

If the username is already online, the second login is rejected:

```json
{"type":"login_failed","reason":"user already online"}
```

### Logout And Heartbeat

```json
{"type":"logout"}
{"type":"ping"}
```

Responses:

```json
{"type":"ok","message":"logout"}
{"type":"pong"}
```

`logout` clears the session and asks the network layer to close the connection
after sending the response.

### Direct Chat

Request:

```json
{"type":"dm","to":"bob","body":"hello bob"}
```

Receiver gets:

```json
{"type":"dm","from":"alice","body":"hello bob"}
```

Sender gets:

```json
{"type":"ok","message":"sent"}
```

If `bob` exists but is offline, the message is still persisted and the sender
gets:

```json
{"type":"error","reason":"receiver offline"}
```

### Group Chat

```json
{"type":"join_group","group_id":100}
{"type":"group_message","group_id":100,"body":"hello group"}
{"type":"leave_group","group_id":100}
```

Other online group members receive:

```json
{"type":"group_message","group_id":100,"from":"alice","body":"hello group"}
```

### History

Direct chat history by username:

```json
{"type":"history","peer":"bob"}
```

Group history:

```json
{"type":"group_history","group_id":100}
```

History response:

```json
{"type":"history_item","chat_type":"dm","from":"alice","to":"bob","body":"hello"}
{"type":"history_end"}
```

## Manual Smoke Test

Open two clients:

```sh
nc 127.0.0.1 9000
```

Client A:

```json
{"type":"register","username":"alice","password":"secret"}
{"type":"login","username":"alice","password":"secret"}
```

Client B:

```json
{"type":"register","username":"bob","password":"secret"}
{"type":"login","username":"bob","password":"secret"}
```

Client A sends:

```json
{"type":"dm","to":"bob","body":"hello bob"}
```

Client B should receive:

```json
{"type":"dm","from":"alice","body":"hello bob"}
```

History:

```json
{"type":"history","peer":"bob"}
```

Group example:

```json
{"type":"join_group","group_id":100}
{"type":"group_message","group_id":100,"body":"hello group"}
{"type":"group_history","group_id":100}
```

## Next Useful Improvements

- Persist group membership.
- Add paginated history queries.
- Add MySQL connection health checks and automatic reconnect.
- Add integration/load-test scripts with latency and throughput output.
- Upgrade from flat JSON to length-prefixed JSON or Protobuf.
