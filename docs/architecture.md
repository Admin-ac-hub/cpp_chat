# Architecture Notes

## Initial Direction

The backend starts as a TCP server with clear module boundaries:

- App: owns startup, shutdown, and dependency wiring.
- Core: owns config and common primitives.
- Network: accepts TCP connections and reads/writes framed messages.
- Protocol: defines message types and serialization rules.
- Session: tracks online users and active connections.
- Chat: routes one-to-one and group messages.
- Storage: persists users, groups, messages, and chat history.
- Logging: records server events and operational errors.

## Dependency Direction

Higher-level modules depend on lower-level boundaries:

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

Network code should not directly own business behavior. It should decode
protocol messages, pass them into the chat/session layer, and write responses
back to connections.

## Protocol

The wire protocol is not finalized yet. Prefer a length-prefixed JSON message
format during early development because it is easy to debug and evolve.
