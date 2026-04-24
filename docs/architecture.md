# Architecture Notes

## Initial Direction

The backend will start as a TCP server with clear module boundaries:

- Network: accepts TCP connections and reads/writes framed messages.
- Session: tracks online users and active connections.
- Chat: routes one-to-one and group messages.
- Storage: persists users, groups, messages, and chat history.
- Logging: records server events and operational errors.

## Protocol

The wire protocol is not finalized yet. Prefer a length-prefixed JSON message
format during early development because it is easy to debug and evolve.

