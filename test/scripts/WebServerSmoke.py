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
import sys
import tempfile
import time
import urllib.parse
import uuid


def exchange(host, port, request, timeout, half_close=False, trickle=False, slow_headers=False):
    started = time.monotonic()
    deadline = started + timeout
    response = bytearray()
    with socket.create_connection((host, port), timeout=timeout) as client:
        client.sendall(request)
        if half_close:
            client.shutdown(socket.SHUT_WR)
        next_byte = started
        next_header = started + 0.4
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
            elif slow_headers and time.monotonic() >= next_header:
                client.sendall(b"X-Slow: x\r\n")
                next_header = time.monotonic() + 0.4
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
        assert response.status == 200, f"{target}: HTTP {response.status}, body: {body!r}"
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
        ("bare CR without LF", prefix + b"X-Test: value\r", 408, 7, False, False),
        ("valid request after faults", prefix + b"Connection: close\r\n\r\n", 200, 5, False, False),
    )
    for name, request, expected, timeout, half_close, trickle in cases:
        status, elapsed = exchange(host, port, request, timeout, half_close, trickle)
        assert status == expected, f"{name}: expected HTTP {expected}, received {status}"
        after = json.loads(get(host, port, "/api/status"))
        assert after["uptime"] >= before["uptime"], f"{name}: device restarted"
        before = after
        print(f"[PASS] {name}: HTTP {status} in {elapsed:.3f}s; server remains available")
    status, elapsed = exchange(host, port, prefix, 12, slow_headers=True)
    assert status == 408, f"slow complete headers: expected HTTP 408, received {status}"
    after = json.loads(get(host, port, "/api/status"))
    assert after["uptime"] >= before["uptime"], "slow complete headers: device restarted"
    print(f"[PASS] slow complete headers: HTTP {status} in {elapsed:.3f}s; server remains available")

    for name, query in (
        ("query separator storm", "path=/&" + "&" * 3500),
        ("more than 256 query arguments", "x=1&" * 256 + "path=/missing"),
    ):
        files = json.loads(get(host, port, "/api/files?" + query))
        assert isinstance(files, list), f"{name}: invalid directory response"
        after = json.loads(get(host, port, "/api/status"))
        assert after["uptime"] >= before["uptime"], f"{name}: device restarted"
        assert isinstance(json.loads(get(host, port, "/api/files?path=/")), list), f"{name}: recovery failed"
        before = after
        print(f"[PASS] {name}: root directory and recovery")
    malformed_multipart(host, port, before)
    print("[PASS] gzip page, directory JSON, request limits, and request recovery")


def malformed_multipart(host, port, before):
    boundary = b"PapyrixBoundary"
    part = b'--' + boundary + b'\r\nContent-Disposition: form-data; name="field"\r\n\r\n'
    end = b"\r\n--" + boundary + b"--\r\n"
    cases = (
        ("oversized multipart disposition", b'--' + boundary + b'\r\nContent-Disposition: form-data; name="' +
         b"x" * 5000 + b'"\r\n\r\nvalue' + end),
        ("oversized multipart field", part + (b"x" * 1000 + b"\r\n") * 17 + b"x" + end),
        ("truncated multipart body", part + b"incomplete"),
    )
    for name, body in cases:
        request = (b"POST /upload?path=/ HTTP/1.1\r\nHost: reader\r\n"
                   b"Content-Type: multipart/form-data; boundary=" + boundary +
                   b"\r\nContent-Length: " + str(len(body)).encode("ascii") +
                   b"\r\nConnection: close\r\n\r\n" + body)
        deadline = time.monotonic() + 8
        with socket.create_connection((host, port), timeout=8) as client:
            try:
                client.sendall(request)
                client.shutdown(socket.SHUT_WR)
                remaining = deadline - time.monotonic()
                assert remaining > 0, f"{name}: termination deadline exceeded"
                client.settimeout(remaining)
                response = client.recv(4096)
                assert not response, f"{name}: expected parser rejection, received {response!r}"
            except (ConnectionResetError, BrokenPipeError):
                pass
        after = json.loads(get(host, port, "/api/status"))
        assert after["uptime"] >= before["uptime"], f"{name}: device restarted"
        before = after
        print(f"[PASS] {name}: bounded termination and recovery")

    for name, opening, chunk in (
        ("continuous multipart headers", part[:-2], b"X-Slow: x\r\n"),
        ("continuous empty multipart field", part, b"\r\n"),
    ):
        started = time.monotonic()
        deadline = started + 12
        request = (b"POST /upload?path=/ HTTP/1.1\r\nHost: reader\r\n"
                   b"Content-Type: multipart/form-data; boundary=" + boundary +
                   b"\r\nContent-Length: 100000\r\nConnection: close\r\n\r\n" + opening)
        with socket.create_connection((host, port), timeout=12) as client:
            client.sendall(request)
            try:
                while True:
                    remaining = deadline - time.monotonic()
                    assert remaining > 0, f"{name}: metadata deadline exceeded"
                    readable, _, _ = select.select([client], [], [], min(remaining, 0.1))
                    if readable:
                        response = client.recv(4096)
                        assert not response, f"{name}: expected parser rejection, received {response!r}"
                        break
                    else:
                        client.sendall(chunk)
            except (ConnectionResetError, BrokenPipeError):
                pass
        after = json.loads(get(host, port, "/api/status"))
        assert after["uptime"] >= before["uptime"], f"{name}: device restarted"
        before = after
        print(f"[PASS] {name}: connection closes in {time.monotonic() - started:.3f}s; server remains available")


def upload_check(host, port):
    name = "webserver-smoke-" + uuid.uuid4().hex + ".txt"
    path = "/" + name
    target = urllib.parse.urlencode({"path": path})
    boundary = "PapyrixUpload" + uuid.uuid4().hex
    opening = (f"--{boundary}\r\n"
               f'Content-Disposition: form-data; name="file"; filename="{name}"\r\n'
               "Content-Type: text/plain\r\n\r\n").encode("ascii")
    closing = f"\r\n--{boundary}--\r\n".encode("ascii")
    data = b"0123456789abcdef" * 4096
    files = json.loads(get(host, port, "/api/files?path=/"))
    assert all(item["name"].lower() != name.lower() for item in files), f"file already exists: {path}"
    attempted = False
    try:
        connection = http.client.HTTPConnection(host, port, timeout=8)
        try:
            connection.putrequest("POST", "/upload?path=/")
            connection.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
            connection.putheader("Content-Length", str(len(opening) + len(data) + len(closing)))
            connection.endheaders()
            attempted = True
            connection.send(opening)
            started = time.monotonic()
            for offset in range(0, len(data), 2048):
                connection.send(data[offset:offset + 2048])
                time.sleep(0.4)
            connection.send(closing)
            response = connection.getresponse()
            body = response.read()
            assert response.status == 200, f"upload: HTTP {response.status}, body: {body!r}"
            assert time.monotonic() - started > 10, "upload body did not exceed the header deadline"
        finally:
            connection.close()
        assert get(host, port, "/download?" + target) == data, "uploaded file content differs"
        files = json.loads(get(host, port, "/api/files?path=/"))
        matches = [item for item in files if item["name"] == name]
        assert len(matches) == 1 and matches[0]["isDirectory"] is False and matches[0]["size"] == len(data), \
            f"uploaded file metadata differs: {matches!r}"
    finally:
        if attempted:
            failed = sys.exc_info()[0] is not None
            try:
                connection = http.client.HTTPConnection(host, port, timeout=5)
                try:
                    connection.request("POST", "/delete?" + target + "&type=file", body=b"")
                    response = connection.getresponse()
                    body = response.read()
                    assert response.status in (200, 404), f"cleanup: HTTP {response.status}, body: {body!r}"
                finally:
                    connection.close()
                files = json.loads(get(host, port, "/api/files?path=/"))
                assert all(item["name"] != name for item in files), f"cleanup failed: {path} remains"
            except Exception as error:
                if not failed:
                    raise
                print(f"[WARN] Could not verify cleanup of {path}: {error}", file=sys.stderr)
    print("[PASS] slow multipart upload, download, metadata, and cleanup")


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
    try:
        data = json.loads(body)
    except (ValueError, UnicodeDecodeError) as error:
        raise AssertionError(f"{target}: invalid JSON, body: {body!r}") from error
    if target == "/api/status":
        assert isinstance(data, dict) and isinstance(data.get("uptime"), int) and isinstance(data.get("freeHeap"), int), \
            f"{target}: invalid status response, body: {body!r}"
        return data
    assert isinstance(data, list) and all(
        isinstance(item, dict) and isinstance(item.get("name"), str) and isinstance(item.get("isDirectory"), bool)
        for item in data
    ), f"{target}: invalid directory response, body: {body!r}"
    return None


def run_load(host, port, rounds):
    baseline = subprocess.run(
        ["ping", "-n", "-c", "5", "-i", "0.2", "-W", "1", host],
        capture_output=True, text=True, timeout=10, check=False,
        env={**os.environ, "LC_ALL": "C"},
    )
    sent, received = ping_counts(baseline.stdout)
    assert sent == received == 5, "ping loss before load; check the local network"
    before = load_request(host, port, "/api/status")
    uptime = before["uptime"]
    minimum_heap = before["freeHeap"]
    started = time.monotonic()
    requests = 0
    # Two page/status/list groups reproduce a browser beside an HTTP probe.
    targets = ("/", "/api/status", "/api/files?path=/") * 2
    iteration = 0
    ping_output = ""
    with tempfile.TemporaryFile(mode="w+") as ping_log:
        ping = subprocess.Popen(
            ["ping", "-n", "-i", "0.2", "-W", "1", host],
            stdout=ping_log, stderr=subprocess.STDOUT,
            env={**os.environ, "LC_ALL": "C"},
        )
        try:
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
                try:
                    ping.send_signal(signal.SIGINT)
                    ping.wait(timeout=5)
                finally:
                    ping_log.seek(0)
                    ping_output = ping_log.read()
            sent, received = ping_counts(ping_output)
            sequences = [int(sequence) for sequence in re.findall(r"bytes from [^\n]+icmp_seq=(\d+)", ping_output)]
            boundaries = [0, *sequences, sent + 1]
            missed = max(right - left - 1 for left, right in zip(boundaries, boundaries[1:]))
            # Five missing replies at 0.2-second intervals indicate a one-second outage.
            assert sent > 0 and missed < 5, f"sustained ping loss under HTTP load: {missed} consecutive replies missing"
            after = load_request(host, port, "/api/status")
            assert after["uptime"] >= uptime, "reader restarted after load"
        except BaseException:
            print(f"[LOAD] failure at round {iteration + 1}/{rounds} in {time.monotonic() - started:.1f}s")
            print(f"[LOAD] ping output:\n{ping_output}", end="" if ping_output.endswith("\n") else "\n")
            raise
    print(f"[PASS] concurrent load: {requests} responses, {received}/{sent} pings, "
          f"maximum {missed} consecutive replies missing; "
          f"minimum sampled heap {minimum_heap} bytes in {time.monotonic() - started:.1f}s")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--load-rounds", type=int, default=0,
                        help="also run N concurrent page/status/list rounds with Linux ping (default: disabled)")
    parser.add_argument("--upload-check", action="store_true",
                        help="also upload, verify, and delete a unique file in the root directory")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be in 1..65535")
    if args.load_rounds < 0:
        parser.error("--load-rounds must be non-negative")
    try:
        run(args.host, args.port)
        if args.upload_check:
            upload_check(args.host, args.port)
        if args.load_rounds:
            run_load(args.host, args.port, args.load_rounds)
    except (AssertionError, OSError, ValueError, http.client.HTTPException, subprocess.SubprocessError) as error:
        print(f"[FAIL] {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
