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
├── include/          # Public headers
├── src/              # Server implementation
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

