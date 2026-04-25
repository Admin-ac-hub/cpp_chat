# cpp_chat

A TCP-based chat backend in C++.

## Planned Features

- One-to-one chat
- Group chat
- Chat history persistence
- Server logs
- User sessions and connection management
- Extensible storage layer

## Project Layout

```text
.
├── CMakeLists.txt
├── include/cpp_chat/ # Public module headers
├── src/              # Module implementations
├── tests/            # Unit and integration tests
├── config/           # Runtime config files
├── logs/             # Local runtime logs, ignored by git
├── docs/             # Design notes and protocol docs
└── scripts/          # Development and operational scripts
```

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/cpp_chat_server
```

## Docker Test

Run the server with MySQL in Docker:

```sh
docker compose -f docker/docker-compose.yml up --build
```

The compose stack starts:

- `mysql`: MySQL 8.4 with `sql/mysql` initialization scripts.
- `app`: Linux build of `cpp_chat_server`, exposed on port `9000`.

Test from another terminal:

```sh
nc 127.0.0.1 9000
```

Check logs in MySQL:

```sh
docker compose -f docker/docker-compose.yml exec mysql mysql -ucpp_chat -pcpp_chat cpp_chat \
  -e "SELECT id, created_at, level, message FROM server_logs ORDER BY id DESC LIMIT 10;"
```

## MySQL Logs

Server logs are written to MySQL table `server_logs`. The SQL files live under
`sql/mysql`.

Default connection:

```text
host: 127.0.0.1
port: 3306
user: cpp_chat
password: cpp_chat
database: cpp_chat
```

Default config file:

```text
config/mysql.env
```

The server reads this file automatically on startup:

```text
CPP_CHAT_LOG_DB_HOST=127.0.0.1
CPP_CHAT_LOG_DB_PORT=3306
CPP_CHAT_LOG_DB_USER=cpp_chat
CPP_CHAT_LOG_DB_PASSWORD=cpp_chat
CPP_CHAT_LOG_DB_NAME=cpp_chat
```

Environment variables with the same names override the file values.

Initialize a local database:

```sh
mysql -u root -p < sql/mysql/schema.sql
mysql -u root -p < sql/mysql/create_user.sql
```

The server also creates `server_logs` automatically after connecting, but the
database and user still need to exist first unless you run the scripts above.

The current TCP server implementation targets Linux and uses `epoll`. It listens
on `0.0.0.0:9000` and echoes received bytes back to the client.

Quick manual test on Linux:

```sh
nc 127.0.0.1 9000
```

Minimal single-chat protocol:

```text
LOGIN <user_id> <username>
DM <receiver_id> <message>
```

Example with two clients:

```text
# client A
LOGIN 1 alice
DM 2 hello

# client B
LOGIN 2 bob
```

If user `2` is online, client B receives:

```text
FROM 1 hello
```

## Current Modules

- `app`: application composition and lifecycle.
- `core`: shared configuration and common types.
- `network`: TCP listener, connections, and socket I/O.
- `protocol`: client/server message model and wire protocol.
- `session`: online users and connection binding.
- `chat`: one-to-one and group message routing.
- `storage`: chat history persistence boundary.
- `logging`: server logs and diagnostics.
