#!/usr/bin/env python3
"""Basic TCP load test for cpp_chat length-prefixed JSON protocol."""

import argparse
import collections
import json
import socket
import statistics
import struct
import sys
import threading
import time


# ---------------------------------------------------------------------------
# MESSAGE TEMPLATES
#
# The default benchmark uses heartbeat/ping because it does not require seeded
# users. If you want to benchmark login or dm later, adjust these templates and
# the worker flow in one place.
# ---------------------------------------------------------------------------

TYPE = "type"
PING = "ping"
PONG = "pong"


def heartbeat_request(worker_id, sequence):
    # The server currently ignores extra fields, so keep the payload minimal.
    return {TYPE: PING}


def is_heartbeat_response(obj):
    return obj.get(TYPE) == PONG


SOCKET_TIMEOUT_SECONDS = 5.0


class LoadStats:
    def __init__(self):
        self.lock = threading.Lock()
        self.success = 0
        self.failed = 0
        self.latencies_ms = []
        self.errors = collections.Counter()

    def record_success(self, latency_ms):
        with self.lock:
            self.success += 1
            self.latencies_ms.append(latency_ms)

    def record_failure(self, reason):
        with self.lock:
            self.failed += 1
            self.errors[str(reason)] += 1


def send_packet(sock, obj):
    payload = json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    sock.sendall(struct.pack("!I", len(payload)) + payload)


def recv_exact(sock, n):
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data.extend(chunk)
    return bytes(data)


def recv_packet(sock):
    header = recv_exact(sock, 4)
    (payload_size,) = struct.unpack("!I", header)
    if payload_size == 0:
        raise ValueError("empty payload")
    payload = recv_exact(sock, payload_size)
    return json.loads(payload.decode("utf-8"))


def run_request(sock, worker_id, sequence):
    request = heartbeat_request(worker_id, sequence)
    started = time.perf_counter()
    send_packet(sock, request)
    response = recv_packet(sock)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    if not is_heartbeat_response(response):
        raise RuntimeError(f"unexpected response: {response}")
    return elapsed_ms


def worker(worker_id, host, port, warmup_count, message_count, stats):
    try:
        with socket.create_connection((host, port), timeout=SOCKET_TIMEOUT_SECONDS) as sock:
            sock.settimeout(SOCKET_TIMEOUT_SECONDS)

            for i in range(warmup_count):
                run_request(sock, worker_id, -i - 1)

            for i in range(message_count):
                try:
                    latency_ms = run_request(sock, worker_id, i)
                    stats.record_success(latency_ms)
                except Exception as exc:
                    stats.record_failure(exc)
    except Exception as exc:
        # Count all messages assigned to this worker as failed if it cannot
        # establish or keep a connection.
        for _ in range(message_count):
            stats.record_failure(exc)


def split_evenly(total, buckets):
    base = total // buckets
    remainder = total % buckets
    return [base + (1 if i < remainder else 0) for i in range(buckets)]


def percentile(sorted_values, percentile_value):
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]

    rank = (len(sorted_values) - 1) * (percentile_value / 100.0)
    lower = int(rank)
    upper = min(lower + 1, len(sorted_values) - 1)
    weight = rank - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight


def print_report(args, stats, elapsed_seconds):
    latencies = sorted(stats.latencies_ms)
    avg = statistics.mean(latencies) if latencies else 0.0
    p50 = percentile(latencies, 50)
    p95 = percentile(latencies, 95)
    p99 = percentile(latencies, 99)
    qps = stats.success / elapsed_seconds if elapsed_seconds > 0 else 0.0

    print(f"connections: {args.connections}")
    print(f"messages: {args.messages}")
    print(f"success: {stats.success}")
    print(f"failed: {stats.failed}")
    print(f"avg latency: {avg:.2f} ms")
    print(f"p50 latency: {p50:.2f} ms")
    print(f"p95 latency: {p95:.2f} ms")
    print(f"p99 latency: {p99:.2f} ms")
    print(f"qps: {qps:.2f}")

    if stats.errors:
        print("errors:")
        for reason, count in stats.errors.most_common(5):
            print(f"  {count}x {reason}")


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Run cpp_chat TCP load test.")
    parser.add_argument("--host", default="127.0.0.1", help="server host, default: 127.0.0.1")
    parser.add_argument("--port", type=int, default=9000, help="server port, default: 9000")
    parser.add_argument("--connections", type=int, default=100, help="number of TCP connections")
    parser.add_argument("--messages", type=int, default=1000, help="total measured messages")
    parser.add_argument("--warmup", type=int, default=100, help="total warmup messages, not measured")
    args = parser.parse_args(argv)

    if args.connections <= 0:
        parser.error("--connections must be positive")
    if args.messages < 0:
        parser.error("--messages must be non-negative")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")

    return args


def main(argv=None):
    args = parse_args(argv)
    stats = LoadStats()

    messages_per_worker = split_evenly(args.messages, args.connections)
    warmup_per_worker = split_evenly(args.warmup, args.connections)

    threads = []
    started = time.perf_counter()
    for worker_id in range(args.connections):
        thread = threading.Thread(
            target=worker,
            args=(
                worker_id,
                args.host,
                args.port,
                warmup_per_worker[worker_id],
                messages_per_worker[worker_id],
                stats,
            ),
            daemon=False,
        )
        threads.append(thread)
        thread.start()

    for thread in threads:
        thread.join()

    elapsed = time.perf_counter() - started
    print_report(args, stats, elapsed)

    return 0 if stats.failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
