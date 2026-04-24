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

The current TCP server implementation uses Linux `epoll`. On Linux it listens
on `0.0.0.0:9000` and echoes received bytes back to the client. On non-Linux
platforms the binary builds, but the server logs that `epoll` is unavailable.

Quick manual test on Linux:

```sh
nc 127.0.0.1 9000
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
