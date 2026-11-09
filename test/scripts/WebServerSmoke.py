#!/usr/bin/env python3
"""Check HTTP limits and optional concurrent-load recovery on a reader."""

import argparse
import concurrent.futures
import gzip
import http.client
import json
import os
import re
import select
import signal
import socket
import subprocess
import tempfile
import time
import urllib.parse
import uuid


def exchange(host, port, request, timeout, half_close=False, trickle=False):
    started = time.monotonic()
    deadline = started + timeout
    response = bytearray()
    with socket.create_connection((host, port), timeout=timeout) as client:
        client.sendall(request)
        if half_close:
            client.shutdown(socket.SHUT_WR)
        next_byte = started
        while b"\r\n" not in response:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError("request exceeds the response deadline")
            readable, _, _ = select.select([client], [], [], min(remaining, 0.1))
            if readable:
                data = client.recv(4096)
                if not data:
                    break
                response.extend(data)
            elif trickle and time.monotonic() >= next_byte:
                client.sendall(b"x")
                next_byte = time.monotonic() + 0.2
    first_line = bytes(response).split(b"\r\n", 1)[0].split()
    assert len(first_line) >= 2, f"missing HTTP response: {bytes(response)!r}"
    return int(first_line[1]), time.monotonic() - started


def get(host, port, target):
    connection = http.client.HTTPConnection(host, port, timeout=5)
    try:
        connection.request("GET", target)
        response = connection.getresponse()
        body = response.read()
        assert response.status == 200, f"{target}: HTTP {response.status}"
        return body
    finally:
        connection.close()


def run(host, port):
    before = json.loads(get(host, port, "/api/status"))
    page = gzip.decompress(get(host, port, "/"))
    assert b"PapyriX" in page and b"/api/files" in page
    assert isinstance(json.loads(get(host, port, "/api/files?path=/")), list)
    prefix = f"GET /api/status HTTP/1.1\r\nHost: {host}\r\n".encode("ascii")
    # URL encoding expands this 1023-byte path beyond 2048 bytes.
    path = "/" + uuid.uuid4().hex + "-t" + ("/" + "я" * 123) * 4
    target = "/api/files?" + urllib.parse.urlencode({"path": path})
    cases = (
        ("maximum-length UTF-8 path", f"GET {target} HTTP/1.1\r\nHost: reader\r\n\r\n".encode("ascii"), 404, 5, False, False),
        ("oversized header", prefix + b"X-Test: " + b"x" * 8192 + b"\r\n\r\n", 431, 5, False, False),
        ("oversized URI", b"GET /" + b"x" * 5000 + b" HTTP/1.1\r\nHost: reader\r\n\r\n", 414, 5, False, False),
        ("incomplete headers with EOF", prefix, 408, 7, True, False),
        ("continuously incomplete header", prefix + b"X-Test: ", 408, 7, False, True),
        ("valid request after faults", prefix + b"Connection: close\r\n\r\n", 200, 5, False, False),
    )
    for name, request, expected, timeout, half_close, trickle in cases:
        status, elapsed = exchange(host, port, request, timeout, half_close, trickle)
        assert status == expected, f"{name}: expected HTTP {expected}, received {status}"
        after = json.loads(get(host, port, "/api/status"))
        assert after["uptime"] >= before["uptime"], f"{name}: device restarted"
        before = after
        print(f"[PASS] {name}: HTTP {status} in {elapsed:.3f}s; server remains available")
    print("[PASS] gzip page, directory JSON, request limits, and request recovery")


def ping_counts(output):
    summary = re.search(r"(\d+) packets transmitted, (\d+) received", output)
    assert summary, f"missing ping summary: {output}"
    return tuple(map(int, summary.groups()))


def load_request(host, port, target):
    body = get(host, port, target)
    if target == "/":
        page = gzip.decompress(body)
        assert b"PapyriX" in page and b"/api/files" in page, "invalid page response"
        return None
    data = json.loads(body)
    if target == "/api/status":
        assert isinstance(data["uptime"], int) and isinstance(data["freeHeap"], int)
        return data
    assert isinstance(data, list), "invalid directory response"
    assert all(isinstance(item["name"], str) and isinstance(item["isDirectory"], bool) for item in data)
    return None


def run_load(host, port, rounds):
    baseline = subprocess.run(
        ["ping", "-n", "-c", "5", "-i", "0.2", "-W", "1", host],
        capture_output=True, text=True, timeout=10, check=False,
        env={**os.environ, "LC_ALL": "C"},
    )
    sent, received = ping_counts(baseline.stdout)
    assert sent == received == 5, "ping loss before load; check the local network"
    before = json.loads(get(host, port, "/api/status"))
    uptime = before["uptime"]
    minimum_heap = before["freeHeap"]
    started = time.monotonic()
    requests = 0
    # Two page/status/list groups reproduce a browser beside an HTTP probe.
    targets = ("/", "/api/status", "/api/files?path=/") * 2
    with tempfile.TemporaryFile(mode="w+") as ping_log:
        ping = subprocess.Popen(
            ["ping", "-n", "-i", "0.2", "-W", "1", host],
            stdout=ping_log, stderr=subprocess.STDOUT,
            env={**os.environ, "LC_ALL": "C"},
        )
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=len(targets)) as pool:
                for iteration in range(rounds):
                    results = list(pool.map(lambda target: load_request(host, port, target), targets))
                    requests += len(targets)
                    statuses = [result for result in results if result is not None]
                    assert all(status["uptime"] >= uptime for status in statuses), "reader restarted under load"
                    uptime = max(status["uptime"] for status in statuses)
                    minimum_heap = min(minimum_heap, *(status["freeHeap"] for status in statuses))
                    if (iteration + 1) % 20 == 0:
                        print(f"[LOAD] {iteration + 1}/{rounds} rounds; minimum sampled heap {minimum_heap} bytes",
                              flush=True)
                    time.sleep(0.75)
        finally:
            ping.send_signal(signal.SIGINT)
            ping.wait(timeout=5)
        ping_log.seek(0)
        ping_output = ping_log.read()
        sent, received = ping_counts(ping_output)
    sequences = [int(sequence) for sequence in re.findall(r"bytes from [^\n]+icmp_seq=(\d+)", ping_output)]
    boundaries = [0, *sequences, sent + 1]
    missed = max(right - left - 1 for left, right in zip(boundaries, boundaries[1:]))
    # Five missing replies at 0.2-second intervals indicate a one-second outage.
    assert sent > 0 and missed < 5, f"sustained ping loss under HTTP load: {missed} consecutive replies missing"
    after = json.loads(get(host, port, "/api/status"))
    assert after["uptime"] >= uptime, "reader restarted after load"
    print(f"[PASS] concurrent load: {requests} responses, {received}/{sent} pings, "
          f"maximum {missed} consecutive replies missing; "
          f"minimum sampled heap {minimum_heap} bytes in {time.monotonic() - started:.1f}s")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--load-rounds", type=int, default=0,
                        help="also run N concurrent page/status/list rounds with Linux ping (default: disabled)")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be in 1..65535")
    if args.load_rounds < 0:
        parser.error("--load-rounds must be non-negative")
    try:
        run(args.host, args.port)
        if args.load_rounds:
            run_load(args.host, args.port, args.load_rounds)
    except (AssertionError, OSError, ValueError, http.client.HTTPException, subprocess.SubprocessError) as error:
        print(f"[FAIL] {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
