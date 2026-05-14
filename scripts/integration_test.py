#!/usr/bin/env python3
"""End-to-end framed TCP integration test for cpp_chat."""

import argparse
import json
import os
import socket
import struct
import sys
import time


TYPE = "type"
USERNAME = "username"
PASSWORD = "password"
TO = "to"
FROM = "from"
BODY = "body"
PEER = "peer"
REASON = "reason"
GROUP_ID = "group_id"
NAME = "name"
MESSAGE_ID = "message_id"
DELIVERED = "delivered"
STORED = "stored"
LAST_SEEN_MESSAGE_ID = "last_seen_message_id"
LIMIT = "limit"
BEFORE_ID = "before_id"
HAS_MORE = "has_more"
NEXT_BEFORE_ID = "next_before_id"

REGISTER = "register"
REGISTER_SUCCESS = "register_success"
LOGIN = "login"
LOGIN_SUCCESS = "login_success"
LOGIN_FAILED = "login_failed"
LOGOUT = "logout"
DM = "dm"
MESSAGE_ACK = "message_ack"
HISTORY = "history"
GROUP_HISTORY = "group_history"
HISTORY_ITEM = "history_item"
HISTORY_END = "history_end"
UNREAD = "unread"
UNREAD_END = "unread_end"
CREATE_GROUP = "create_group"
CREATE_GROUP_SUCCESS = "create_group_success"
JOIN_GROUP = "join_group"
GROUP_MESSAGE = "group_message"
OK = "ok"
ERROR = "error"

MESSAGE_TIMEOUT_SECONDS = 5.0


class IntegrationFailure(Exception):
    pass


def log_pass(message):
    print(f"[PASS] {message}")


def fail(message):
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def send_packet(sock, obj):
    payload = json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    header = struct.pack("!I", len(payload))
    sock.sendall(header + payload)


def recv_exact(sock, n):
    chunks = bytearray()
    while len(chunks) < n:
        try:
            chunk = sock.recv(n - len(chunks))
        except socket.timeout as exc:
            raise IntegrationFailure(f"timeout while reading {n} bytes") from exc
        except OSError as exc:
            raise IntegrationFailure(f"socket recv failed: {exc}") from exc

        if not chunk:
            raise IntegrationFailure("connection closed while reading packet")
        chunks.extend(chunk)
    return bytes(chunks)


def recv_packet(sock):
    header = recv_exact(sock, 4)
    (payload_size,) = struct.unpack("!I", header)
    if payload_size == 0:
        raise IntegrationFailure("received invalid empty payload")
    payload = recv_exact(sock, payload_size)
    try:
        return json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise IntegrationFailure(f"invalid JSON payload: {payload!r}") from exc


def assert_type(obj, expected_type, step_name):
    actual_type = obj.get(TYPE)
    if actual_type != expected_type:
        raise IntegrationFailure(
            f"{step_name}: expected type={expected_type!r}, got {actual_type!r}, response={obj}"
        )


def assert_field(obj, field, expected, step_name):
    actual = obj.get(field)
    if actual != expected:
        raise IntegrationFailure(
            f"{step_name}: expected {field}={expected!r}, got {actual!r}, response={obj}"
        )


def register_user(sock, username, password, label):
    send_packet(sock, {
        TYPE: REGISTER,
        USERNAME: username,
        PASSWORD: password,
    })
    response = recv_packet(sock)
    assert_type(response, REGISTER_SUCCESS, f"register {label}")
    log_pass(f"register {label}")
    return response


def login_user(sock, username, password, label):
    send_packet(sock, {
        TYPE: LOGIN,
        USERNAME: username,
        PASSWORD: password,
    })
    response = recv_packet(sock)
    assert_type(response, LOGIN_SUCCESS, f"login {label}")
    assert_field(response, USERNAME, username, f"login {label}")
    log_pass(f"login {label}")
    return response


def assert_duplicate_login_rejected(host, port, username, password):
    with socket.create_connection((host, port), timeout=MESSAGE_TIMEOUT_SECONDS) as duplicate:
        duplicate.settimeout(MESSAGE_TIMEOUT_SECONDS)
        send_packet(duplicate, {
            TYPE: LOGIN,
            USERNAME: username,
            PASSWORD: password,
        })
        response = recv_packet(duplicate)
        assert_type(response, LOGIN_FAILED, "duplicate login")
        assert_field(response, REASON, "user already online", "duplicate login")
    log_pass("duplicate login rejected")


def assert_unauthenticated_dm_rejected(host, port):
    with socket.create_connection((host, port), timeout=MESSAGE_TIMEOUT_SECONDS) as guest:
        guest.settimeout(MESSAGE_TIMEOUT_SECONDS)
        send_packet(guest, {
            TYPE: DM,
            TO: "nobody",
            BODY: "should fail",
        })
        response = recv_packet(guest)
        assert_type(response, ERROR, "unauthenticated dm")
        assert_field(response, REASON, "please login first", "unauthenticated dm")
    log_pass("unauthenticated dm rejected")


def logout_user(sock, label):
    send_packet(sock, {
        TYPE: LOGOUT,
    })
    response = recv_packet(sock)
    assert_type(response, OK, f"logout {label}")
    log_pass(f"logout {label}")


def assert_disconnect_cleanup_allows_login(host, port, username, password):
    deadline = time.time() + MESSAGE_TIMEOUT_SECONDS
    last_response = None

    while time.time() < deadline:
        with socket.create_connection((host, port), timeout=MESSAGE_TIMEOUT_SECONDS) as probe:
            probe.settimeout(MESSAGE_TIMEOUT_SECONDS)
            send_packet(probe, {
                TYPE: LOGIN,
                USERNAME: username,
                PASSWORD: password,
            })
            response = recv_packet(probe)
            last_response = response
            if response.get(TYPE) == LOGIN_SUCCESS:
                assert_field(response, USERNAME, username, "disconnect cleanup login")
                logout_user(probe, "disconnect cleanup probe")
                log_pass("disconnect cleanup allows login")
                return
            if response.get(TYPE) != LOGIN_FAILED:
                raise IntegrationFailure(f"unexpected disconnect cleanup response: {response}")
        time.sleep(0.1)

    raise IntegrationFailure(f"disconnect cleanup did not allow login: {last_response}")


def send_dm(sender_sock, receiver_sock, sender_name, receiver_name, message_body):
    send_packet(sender_sock, {
        TYPE: DM,
        TO: receiver_name,
        BODY: message_body,
    })

    delivered = recv_packet(receiver_sock)
    assert_type(delivered, DM, "deliver dm")
    assert_field(delivered, FROM, sender_name, "deliver dm")
    assert_field(delivered, BODY, message_body, "deliver dm")
    log_pass("client_b received dm")

    ack = recv_packet(sender_sock)
    assert_type(ack, MESSAGE_ACK, "sender dm ack")
    assert_field(ack, STORED, True, "sender dm ack")
    assert_field(ack, DELIVERED, True, "sender dm ack")
    log_pass("client_a dm ack")
    return ack


def send_offline_dm(sender_sock, receiver_name, message_body):
    send_packet(sender_sock, {
        TYPE: DM,
        TO: receiver_name,
        BODY: message_body,
    })
    ack = recv_packet(sender_sock)
    assert_type(ack, MESSAGE_ACK, "offline dm ack")
    assert_field(ack, STORED, True, "offline dm ack")
    assert_field(ack, DELIVERED, False, "offline dm ack")
    log_pass("offline dm stored")
    return ack


def assert_unread_contains(sock, last_seen_message_id, sender_name, receiver_name, message_body):
    items, _ = recv_history_stream(sock, {
        TYPE: UNREAD,
        LAST_SEEN_MESSAGE_ID: last_seen_message_id,
    }, UNREAD_END, "unread")

    for item in items:
        if (
            item.get(FROM) == sender_name
            and item.get(TO) == receiver_name
            and item.get(BODY) == message_body
        ):
            log_pass("unread contains offline dm")
            return

    raise IntegrationFailure("unread did not contain expected offline dm")


def create_group(sock, name):
    send_packet(sock, {
        TYPE: CREATE_GROUP,
        NAME: name,
    })
    response = recv_packet(sock)
    assert_type(response, CREATE_GROUP_SUCCESS, "create group")
    log_pass("create group")
    return response.get(GROUP_ID)


def join_group(sock, group_id, label):
    send_packet(sock, {
        TYPE: JOIN_GROUP,
        GROUP_ID: group_id,
    })
    response = recv_packet(sock)
    assert_type(response, OK, f"join group {label}")
    log_pass(f"join group {label}")


def send_group_message(sender_sock, receiver_sock, group_id, sender_name, message_body):
    send_packet(sender_sock, {
        TYPE: GROUP_MESSAGE,
        GROUP_ID: group_id,
        BODY: message_body,
    })

    delivered = recv_packet(receiver_sock)
    assert_type(delivered, GROUP_MESSAGE, "deliver group message")
    assert_field(delivered, GROUP_ID, group_id, "deliver group message")
    assert_field(delivered, FROM, sender_name, "deliver group message")
    assert_field(delivered, BODY, message_body, "deliver group message")

    ack = recv_packet(sender_sock)
    assert_type(ack, MESSAGE_ACK, "group message ack")
    assert_field(ack, STORED, True, "group message ack")
    assert_field(ack, DELIVERED, True, "group message ack")
    log_pass("group message delivered and acked")
    return ack


def recv_history_stream(sock, request, expected_end_type, step_name):
    send_packet(sock, request)

    items = []
    while True:
        response = recv_packet(sock)
        response_type = response.get(TYPE)

        if response_type == expected_end_type:
            return items, response

        if response_type == ERROR:
            reason = response.get(REASON, "unknown error")
            raise IntegrationFailure(f"{step_name} returned error: {reason}")

        if response_type != HISTORY_ITEM:
            raise IntegrationFailure(f"unexpected {step_name} response: {response}")

        items.append(response)


def assert_history_contains(sock, peer_name, sender_name, receiver_name, message_body):
    items, _ = recv_history_stream(sock, {
        TYPE: HISTORY,
        PEER: peer_name,
    }, HISTORY_END, "history")

    for item in items:
        if (
            item.get(FROM) == sender_name
            and item.get(TO) == receiver_name
            and item.get(BODY) == message_body
        ):
            log_pass("history contains dm")
            return

    raise IntegrationFailure(
        f"history did not contain dm from {sender_name!r} to {receiver_name!r} "
        f"with body {message_body!r}; checked {len(items)} item(s)"
    )


def assert_direct_history_cursor_pagination(sock, peer_name, newest_body, older_body):
    first_items, first_end = recv_history_stream(sock, {
        TYPE: HISTORY,
        PEER: peer_name,
        LIMIT: 1,
    }, HISTORY_END, "direct history first page")

    if len(first_items) != 1:
        raise IntegrationFailure(f"expected one direct history item, got {first_items}")
    assert_field(first_items[0], BODY, newest_body, "direct history first page")
    assert_field(first_end, HAS_MORE, True, "direct history first page")
    before_id = first_end.get(NEXT_BEFORE_ID)
    if not isinstance(before_id, int) or before_id <= 0:
        raise IntegrationFailure(f"invalid next_before_id in direct history: {first_end}")

    second_items, second_end = recv_history_stream(sock, {
        TYPE: HISTORY,
        PEER: peer_name,
        LIMIT: 1,
        BEFORE_ID: before_id,
    }, HISTORY_END, "direct history second page")

    if len(second_items) != 1:
        raise IntegrationFailure(f"expected one direct history item on second page, got {second_items}")
    assert_field(second_items[0], BODY, older_body, "direct history second page")
    if second_items[0].get(MESSAGE_ID) == first_items[0].get(MESSAGE_ID):
        raise IntegrationFailure("direct history cursor returned the same message twice")
    assert_field(second_end, HAS_MORE, True, "direct history second page")
    log_pass("direct history cursor pagination")


def assert_group_history_contains(sock, group_id, sender_name, message_body):
    items, _ = recv_history_stream(sock, {
        TYPE: GROUP_HISTORY,
        GROUP_ID: group_id,
    }, HISTORY_END, "group history")

    for item in items:
        if (
            item.get(FROM) == sender_name
            and item.get(TO) == str(group_id)
            and item.get(BODY) == message_body
        ):
            log_pass("group history contains group message")
            return

    raise IntegrationFailure(
        f"group history did not contain message from {sender_name!r} "
        f"to group {group_id}: {message_body!r}"
    )


def assert_group_history_cursor_pagination(sock, group_id, newest_body, older_body):
    first_items, first_end = recv_history_stream(sock, {
        TYPE: GROUP_HISTORY,
        GROUP_ID: group_id,
        LIMIT: 1,
    }, HISTORY_END, "group history first page")

    if len(first_items) != 1:
        raise IntegrationFailure(f"expected one group history item, got {first_items}")
    assert_field(first_items[0], BODY, newest_body, "group history first page")
    assert_field(first_end, HAS_MORE, True, "group history first page")
    before_id = first_end.get(NEXT_BEFORE_ID)
    if not isinstance(before_id, int) or before_id <= 0:
        raise IntegrationFailure(f"invalid next_before_id in group history: {first_end}")

    second_items, _ = recv_history_stream(sock, {
        TYPE: GROUP_HISTORY,
        GROUP_ID: group_id,
        LIMIT: 1,
        BEFORE_ID: before_id,
    }, HISTORY_END, "group history second page")

    if len(second_items) != 1:
        raise IntegrationFailure(f"expected one group history item on second page, got {second_items}")
    assert_field(second_items[0], BODY, older_body, "group history second page")
    if second_items[0].get(MESSAGE_ID) == first_items[0].get(MESSAGE_ID):
        raise IntegrationFailure("group history cursor returned the same message twice")
    log_pass("group history cursor pagination")


def make_username(prefix):
    return f"{prefix}_{int(time.time())}_{os.getpid()}"


def run(host, port):
    password = "secret"
    user_a = make_username("itest_a")
    user_b = make_username("itest_b")
    message_body = f"integration hello {int(time.time())}"

    assert_unauthenticated_dm_rejected(host, port)

    with socket.create_connection((host, port), timeout=MESSAGE_TIMEOUT_SECONDS) as client_a, \
            socket.create_connection((host, port), timeout=MESSAGE_TIMEOUT_SECONDS) as client_b:
        client_a.settimeout(MESSAGE_TIMEOUT_SECONDS)
        client_b.settimeout(MESSAGE_TIMEOUT_SECONDS)

        register_user(client_a, user_a, password, "user_a")
        register_user(client_b, user_b, password, "user_b")

        login_user(client_a, user_a, password, "user_a")
        login_user(client_b, user_b, password, "user_b")
        assert_duplicate_login_rejected(host, port, user_a, password)

        online_ack = send_dm(client_a, client_b, user_a, user_b, message_body)
        assert_history_contains(client_b, user_a, user_a, user_b, message_body)

        client_b.close()
        assert_disconnect_cleanup_allows_login(host, port, user_b, password)
        time.sleep(0.2)

        offline_body = f"offline hello {int(time.time())}"
        older_page_body = f"offline page older {int(time.time())}"
        newest_page_body = f"offline page newest {int(time.time())}"
        send_offline_dm(client_a, user_b, offline_body)
        send_offline_dm(client_a, user_b, older_page_body)
        send_offline_dm(client_a, user_b, newest_page_body)

        with socket.create_connection((host, port), timeout=MESSAGE_TIMEOUT_SECONDS) as client_b2:
            client_b2.settimeout(MESSAGE_TIMEOUT_SECONDS)
            login_user(client_b2, user_b, password, "user_b reconnect")
            assert_duplicate_login_rejected(host, port, user_b, password)
            assert_unread_contains(
                client_b2,
                online_ack[MESSAGE_ID],
                user_a,
                user_b,
                offline_body,
            )
            assert_direct_history_cursor_pagination(
                client_b2,
                user_a,
                newest_page_body,
                older_page_body,
            )

            group_id = create_group(client_a, f"itest_group_{int(time.time())}")
            join_group(client_b2, group_id, "user_b")
            older_group_body = f"group older {int(time.time())}"
            newest_group_body = f"group newest {int(time.time())}"
            send_group_message(client_a, client_b2, group_id, user_a, older_group_body)
            send_group_message(client_a, client_b2, group_id, user_a, newest_group_body)
            assert_group_history_contains(client_b2, group_id, user_a, newest_group_body)
            assert_group_history_cursor_pagination(
                client_b2,
                group_id,
                newest_group_body,
                older_group_body,
            )

    print("Integration test passed.")


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Run cpp_chat end-to-end integration test.")
    parser.add_argument("--host", default="127.0.0.1", help="server host, default: 127.0.0.1")
    parser.add_argument("--port", type=int, default=9000, help="server port, default: 9000")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        run(args.host, args.port)
    except IntegrationFailure as exc:
        fail(str(exc))
    except ConnectionRefusedError:
        fail(f"connection refused: {args.host}:{args.port}; is the server running?")
    except OSError as exc:
        fail(f"socket error: {exc}")


if __name__ == "__main__":
    main()
