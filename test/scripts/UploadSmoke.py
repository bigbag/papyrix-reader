#!/usr/bin/env python3
"""Run opt-in web and LocalSend upload regressions against a device."""

import argparse
import hashlib
import http.client
import json
import pathlib
import socket
import time
import urllib.parse
import uuid


MANIFEST_VERSION = 1
WEB_PORT = 80
LOCALSEND_PORT = 53317
READ_CHUNK = 64 * 1024
LOCAL_SEND_FRAMES = (
    ("valid-chunked", b"4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", b"Wikipedia", 9),
    ("invalid-hex", b"Z\r\n", b"x", 1),
    ("missing-end", b"3\r\nabc\r\n", b"abc", 3),
    ("short-body", b"3\r\nab", b"abc", 3),
    ("overrun", b"4\r\nabcd\r\n0\r\n\r\n", b"abc", 3),
)
BOUNDARY_SIZES = (1, 511, 512, 513, 1435, 1436, 1437, 4095, 4096, 4097)


class RunnerError(Exception):
    pass


def digest_bytes(data):
    return hashlib.sha256(data).hexdigest()


def hash_file(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        while True:
            block = source.read(READ_CHUNK)
            if not block:
                break
            size += len(block)
            digest.update(block)
    return size, digest.hexdigest()


def file_chunks(path, chunk_size=READ_CHUNK, pause=0.0):
    with path.open("rb") as source:
        while True:
            block = source.read(chunk_size)
            if not block:
                break
            yield block
            if pause:
                time.sleep(pause)


def host_port(host, port):
    return (host, port)


def http_request(host, port, method, target, body=None, headers=None, timeout=15.0,
                 encode_chunked=False):
    connection = http.client.HTTPConnection(*host_port(host, port), timeout=timeout)
    started = time.monotonic()
    try:
        connection.request(method, target, body=body, headers=headers or {},
                           encode_chunked=encode_chunked)
        response = connection.getresponse()
        content = response.read()
        return response.status, content, time.monotonic() - started
    finally:
        connection.close()

def download_info(host, port, path, timeout):
    target = "/download?" + urllib.parse.urlencode({"path": path})
    connection = http.client.HTTPConnection(*host_port(host, port), timeout=timeout)
    started = time.monotonic()
    try:
        connection.request("GET", target)
        response = connection.getresponse()
        digest = hashlib.sha256()
        size = 0
        while True:
            block = response.read(READ_CHUNK)
            if not block:
                break
            size += len(block)
            digest.update(block)
        return response.status, size, digest.hexdigest(), time.monotonic() - started
    finally:
        connection.close()


def raw_exchange(host, port, method, target, headers, body_chunks=(), timeout=15.0,
                 half_close=False, stall=0.0, chunk_pause=0.0):
    connection = socket.create_connection(host_port(host, port), timeout=timeout)
    connection.settimeout(timeout)
    started = time.monotonic()
    response = bytearray()
    request_headers = [f"{method} {target} HTTP/1.1", f"Host: {host}"]
    request_headers.extend(f"{key}: {value}" for key, value in headers.items())
    request_headers.extend(("Connection: close", "", ""))
    try:
        connection.sendall("\r\n".join(request_headers).encode("ascii"))
        for index, block in enumerate(body_chunks):
            if block:
                connection.sendall(block)
            if chunk_pause and index + 1 < len(body_chunks):
                time.sleep(chunk_pause)
        if half_close:
            connection.shutdown(socket.SHUT_WR)
        if stall:
            time.sleep(stall)
        while True:
            try:
                block = connection.recv(4096)
            except socket.timeout:
                raise
            except OSError:
                break
            if not block:
                break
            response.extend(block)
    except socket.timeout:
        raise
    except OSError:
        pass
    finally:
        connection.close()
    response = bytes(response)
    first_line = response.split(b"\r\n", 1)[0].split()
    status = 0
    if len(first_line) > 1:
        try:
            status = int(first_line[1])
        except ValueError:
            status = 0
    return status, response, time.monotonic() - started


def raw_response_body(response):
    separator = response.find(b"\r\n\r\n")
    return response[separator + 4:] if separator >= 0 else b""


def form_request(host, port, target, fields, timeout):
    body = urllib.parse.urlencode(fields).encode("ascii")
    return http_request(
        host,
        port,
        "POST",
        target,
        body,
        {"Content-Type": "application/x-www-form-urlencoded", "Content-Length": str(len(body))},
        timeout,
    )


def fresh_manifest(host):
    return {
        "version": MANIFEST_VERSION,
        "host": host,
        "run_prefix": "upload-" + uuid.uuid4().hex,
        "web_dir": None,
        "created_dirs": [],
        "items": [],
        "failures": [],
    }


def valid_prefix(prefix):
    return (isinstance(prefix, str) and len(prefix) == 39 and prefix.startswith("upload-")
            and all(char in "0123456789abcdef" for char in prefix[7:]))


def load_manifest(path, host, create=True):
    if path.exists():
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise RunnerError("cannot read manifest JSON") from error
        if not isinstance(manifest, dict) or manifest.get("version") != MANIFEST_VERSION:
            raise RunnerError("unsupported or tampered manifest")
        if manifest.get("host") != host or not valid_prefix(manifest.get("run_prefix")):
            raise RunnerError("manifest host or run prefix does not match")
        if not isinstance(manifest.get("items"), list) or not isinstance(manifest.get("failures"), list):
            raise RunnerError("manifest item lists are invalid")
        if not isinstance(manifest.get("created_dirs"), list):
            raise RunnerError("manifest directory list is invalid")
        if manifest.get("web_dir") not in (None, "/" + manifest["run_prefix"]):
            raise RunnerError("manifest web directory is not run-owned")
        verify_manifest_paths(manifest)
        return manifest
    if not create:
        raise RunnerError("manifest does not exist")
    manifest = fresh_manifest(host)
    save_manifest(path, manifest)
    return manifest


def save_manifest(path, manifest):
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def append_item(path, manifest, case, expected_path, size, sha256, status, duration,
                expected, absence_checked=False, kind=None):
    item = {
        "case": case,
        "path": expected_path,
        "size": size,
        "sha256": sha256,
        "http_result": status,
        "duration_seconds": round(duration, 3),
        "expected": expected,
        "absence_checked": bool(absence_checked),
    }
    if kind:
        item["kind"] = kind
    manifest["items"].append(item)
    save_manifest(path, manifest)


def fail(path, manifest, case, reason):
    print(f"[FAIL] {case}: {reason}")
    manifest["failures"].append({"case": case, "reason": reason})
    save_manifest(path, manifest)


def check_status(path, manifest, case, actual, expected):
    if actual != expected:
        fail(path, manifest, case, f"expected HTTP {expected}, received {actual or 'connection close'}")
        return False
    print(f"[PASS] {case}: HTTP {actual}")
    return True


def check_non_success(path, manifest, case, actual):
    if actual == 200:
        fail(path, manifest, case, "malformed or incomplete request returned HTTP 200")
        return False
    print(f"[PASS] {case}: rejected (HTTP {actual or 'connection close'})")
    return True


def safe_file_path(manifest, kind, path):
    prefix = manifest["run_prefix"]
    if not isinstance(path, str) or not path.startswith("/") or "\\" in path:
        return False
    components = path.split("/")
    if any(component in ("", ".", "..") for component in components[1:]):
        return False
    name = components[-1]
    if not name.startswith(prefix + "-"):
        return False
    if any(char not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-?" for char in name):
        return False
    if kind == "web":
        return manifest.get("web_dir") == "/" + prefix and components == ["", prefix, name]
    if kind == "localsend":
        return components == ["", "received", name]
    return False


def record_control(path, manifest, case, body, status, duration):
    append_item(path, manifest, case, None, len(body), digest_bytes(body), status,
                duration, "control")


def manifest_failures(manifest):
    if manifest["failures"]:
        print(f"[FAIL] {len(manifest['failures'])} earlier check(s) remain failed")
        return 1
    return 0


def multipart_parts(filename, boundary):
    opening = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode("ascii")
    closing = f"\r\n--{boundary}--\r\n".encode("ascii")
    return opening, closing


def multipart_iter(opening, closing, source_chunks):
    yield opening
    yield from source_chunks
    yield closing


def web_upload(host, port, timeout, directory, filename, size, source_chunks):
    boundary = "PapyrixUpload" + uuid.uuid4().hex
    opening, closing = multipart_parts(filename, boundary)
    body = multipart_iter(opening, closing, source_chunks)
    target = "/upload?" + urllib.parse.urlencode({"path": directory})
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(opening) + size + len(closing)),
    }
    return http_request(host, port, "POST", target, body, headers, timeout)


def small_source(data):
    if data:
        yield data


def web_case(path, manifest, host, port, timeout, case, filename, data=None, source=None,
             size=None, sha256=None, expect_success=True):
    expected_path = manifest["web_dir"] + "/" + filename
    if data is not None:
        size = len(data)
        sha256 = digest_bytes(data)
        chunks = small_source(data)
    else:
        chunks = source
    try:
        status, _body, duration = web_upload(
            host, port, timeout, manifest["web_dir"], filename, size, chunks
        )
    except Exception as error:
        status, duration = 0, 0.0
        fail(path, manifest, case, f"upload transport failed ({type(error).__name__})")
    good_status = check_status(path, manifest, case, status, 200) if expect_success else check_status(
        path, manifest, case, status, 400
    )
    if expect_success:
        append_item(path, manifest, case, expected_path, size, sha256, status,
                    duration, "present", kind="web")
        if good_status:
            try:
                actual, actual_size, actual_hash, _elapsed = download_info(host, port, expected_path, timeout)
            except Exception as error:
                fail(path, manifest, case, f"download transport failed ({type(error).__name__})")
            else:
                if actual != 200 or actual_size != size or actual_hash != sha256:
                    fail(path, manifest, case, "downloaded bytes do not match the upload")
                else:
                    print(f"[PASS] {case}: downloaded size and SHA-256 match")
    else:
        try:
            actual, _actual_size, _actual_hash, _elapsed = download_info(host, port, expected_path, timeout)
        except Exception as error:
            actual = 0
            fail(path, manifest, case, f"absence check failed ({type(error).__name__})")
        absent = actual == 404
        append_item(path, manifest, case, expected_path, size, sha256, status,
                    duration, "absent", absence_checked=absent, kind="web")
        if actual != 404 and actual:
            fail(path, manifest, case, f"rejected output is not absent (download HTTP {actual})")
        elif good_status and absent:
            print(f"[PASS] {case}: rejected output is absent")
    return duration


def web_multiple_files(path, manifest, host, port, timeout):
    boundary = "PapyrixUpload" + uuid.uuid4().hex
    first_name = f"{manifest['run_prefix']}-multi-first.txt"
    second_name = f"{manifest['run_prefix']}-multi-second.txt"
    first = b"first multipart file"
    second = b"second multipart file"
    first_header, closing = multipart_parts(first_name, boundary)
    second_header, _ = multipart_parts(second_name, boundary)
    payload = first_header + first + b"\r\n" + second_header + second + closing
    target = "/upload?" + urllib.parse.urlencode({"path": manifest["web_dir"]})
    try:
        status, _body, duration = http_request(
            host, port, "POST", target, payload,
            {"Content-Type": f"multipart/form-data; boundary={boundary}",
             "Content-Length": str(len(payload))}, timeout,
        )
    except Exception as error:
        status, duration = 0, 0.0
        fail(path, manifest, "web-multiple-files", f"transport failed ({type(error).__name__})")
    check_status(path, manifest, "web-multiple-files", status, 200)
    for filename, expected in ((first_name, first), (second_name, second)):
        case = "web-multiple-" + ("first" if filename == first_name else "second")
        expected_path = manifest["web_dir"] + "/" + filename
        append_item(path, manifest, case, expected_path, len(expected),
                    digest_bytes(expected), status, duration, "present", kind="web")
        try:
            actual, size, digest, _elapsed = download_info(host, port, expected_path, timeout)
        except Exception as error:
            fail(path, manifest, case, f"download failed ({type(error).__name__})")
            continue
        if actual != 200 or size != len(expected) or digest != digest_bytes(expected):
            fail(path, manifest, case, "completed multipart output was deleted or changed")
        else:
            print(f"[PASS] {case}: downloaded size and SHA-256 match")


def web_truncated(path, manifest, host, port, timeout, filename, sample):
    expected_path = manifest["web_dir"] + "/" + filename
    boundary = "PapyrixUpload" + uuid.uuid4().hex
    opening, closing = multipart_parts(filename, boundary)
    target = "/upload?" + urllib.parse.urlencode({"path": manifest["web_dir"]})
    declared = len(opening) + sample.stat().st_size + len(closing)
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(declared),
    }
    first = next(file_chunks(sample, 97), b"")
    payload = opening + first
    try:
        status, _response, duration = raw_exchange(
            host, port, "POST", target, headers, (payload,), timeout, half_close=True
        )
    except Exception as error:
        status, duration = 0, 0.0
        fail(path, manifest, "web-truncated-multipart", f"transport failed ({type(error).__name__})")
    check_non_success(path, manifest, "web-truncated-multipart", status)
    try:
        actual, _size, _hash, _elapsed = download_info(host, port, expected_path, timeout)
    except Exception as error:
        actual = 0
        fail(path, manifest, "web-truncated-multipart",
             f"absence check failed ({type(error).__name__})")
    absent = actual == 404
    sample_size, sample_hash = hash_file(sample)
    append_item(path, manifest, "web-truncated-multipart", expected_path,
                sample_size, sample_hash, status, duration, "absent",
                absence_checked=absent, kind="web")
    if actual and actual != 404:
        fail(path, manifest, "web-truncated-multipart", f"partial output is not absent (download HTTP {actual})")
    elif absent:
        print("[PASS] web-truncated-multipart: partial output is absent")


def run_web(args):
    manifest_path = pathlib.Path(args.manifest)
    sample = pathlib.Path(args.sample)
    if not sample.is_file():
        raise RunnerError("sample file does not exist")
    sample_size, sample_hash = hash_file(sample)
    if args.slow_over_120 and sample_size < 240:
        raise RunnerError("--slow-over-120 requires a sample of at least 240 bytes")
    manifest = load_manifest(manifest_path, args.host)
    prefix = manifest["run_prefix"]
    web_dir = "/" + prefix
    manifest["web_dir"] = web_dir
    save_manifest(manifest_path, manifest)
    status, _body, duration = form_request(
        args.host, args.port, "/mkdir", {"name": prefix, "path": "/"}, args.timeout
    )
    record_control(manifest_path, manifest, "web-create-directory", b"", status, duration)
    if not check_status(manifest_path, manifest, "web-create-directory", status, 200):
        return 1
    if web_dir not in manifest["created_dirs"]:
        manifest["created_dirs"].append(web_dir)
        save_manifest(manifest_path, manifest)

    for index in range(1, args.repeat + 1):
        filename = f"{prefix}-sample-{index:03d}.fb2"
        web_case(manifest_path, manifest, args.host, args.port, args.timeout,
                 f"web-sample-{index}", filename, source=file_chunks(sample),
                 size=sample_size, sha256=sample_hash)

    web_case(manifest_path, manifest, args.host, args.port, args.timeout,
             "web-empty-txt", f"{prefix}-empty.txt", data=b"")
    for size in BOUNDARY_SIZES:
        data = bytes((17 + i * 131) & 0xFF for i in range(size))
        web_case(manifest_path, manifest, args.host, args.port, args.timeout,
                 f"web-boundary-{size}", f"{prefix}-boundary-{size}.txt", data=data)

    web_multiple_files(manifest_path, manifest, args.host, args.port, args.timeout)

    web_case(manifest_path, manifest, args.host, args.port, args.timeout,
             "web-unsupported-suffix", f"{prefix}-unsupported.unsupported",
             data=b"unsupported", expect_success=False)
    web_case(manifest_path, manifest, args.host, args.port, args.timeout,
             "web-malformed-filename", f"{prefix}-bad?.fb2",
             data=b"malformed name", expect_success=False)
    web_truncated(manifest_path, manifest, args.host, args.port, args.timeout,
                  f"{prefix}-retry.fb2", sample)
    web_case(manifest_path, manifest, args.host, args.port, args.timeout,
             "web-truncated-retry", f"{prefix}-retry.fb2", source=file_chunks(sample),
             size=sample_size, sha256=sample_hash)

    if args.slow_over_120:
        chunk_size = max(1, sample_size // 240)
        filename = f"{prefix}-slow-web.fb2"
        elapsed = web_case(manifest_path, manifest, args.host, args.port, args.timeout,
                           "web-slow-over-120", filename,
                           source=file_chunks(sample, chunk_size, 0.53),
                           size=sample_size, sha256=sample_hash)
        if elapsed < 120:
            fail(manifest_path, manifest, "web-slow-over-120", "request did not progress for over 120 seconds")
    return manifest_failures(manifest)


def localsend_target(path, fields):
    return path + "?" + urllib.parse.urlencode(fields)


def prepare_json(host, port, timeout, body):
    return http_request(
        host, port, "POST", "/api/localsend/v2/prepare-upload", body,
        {"Content-Type": "application/json", "Content-Length": str(len(body))}, timeout
    )


def raw_prepare(host, port, timeout, body, declared=None, fragments=False, stall=0.0,
                half_close=False):
    headers = {"Content-Type": "application/json", "Content-Length": str(len(body) if declared is None else declared)}
    if fragments:
        chunks = tuple(body[offset:offset + 7] for offset in range(0, len(body), 7))
        return raw_exchange(host, port, "POST", "/api/localsend/v2/prepare-upload",
                            headers, chunks, timeout, half_close, stall, 0.015)
    return raw_exchange(host, port, "POST", "/api/localsend/v2/prepare-upload",
                        headers, (body,), timeout, half_close, stall)


def cancel_session(host, port, timeout, session_id):
    if not session_id:
        return
    target = localsend_target("/api/localsend/v2/cancel", {"sessionId": session_id})
    try:
        http_request(host, port, "POST", target, b"", {"Content-Length": "0"}, timeout)
    except Exception:
        pass


def make_prepare_body(file_id, filename, size):
    return json.dumps(
        {"files": {file_id: {"id": file_id, "fileName": filename, "size": size}}},
        separators=(",", ":"),
    ).encode("utf-8")
def padded_prepare_body(body, target_size):
    content = body[:-1] + b',"_ignored":"metadata"}'
    if len(content) > target_size:
        raise RunnerError("prepared JSON base exceeds requested size")
    return content + b" " * (target_size - len(content))



def record_prepare_case(manifest_path, manifest, case, body, status, duration):
    record_control(manifest_path, manifest, case, body, status, duration)


def prepare_one(path, manifest, host, port, timeout, case, file_id, filename, size):
    body = make_prepare_body(file_id, filename, size)
    status, response, duration = prepare_json(host, port, timeout, body)
    record_prepare_case(path, manifest, case, body, status, duration)
    if status != 200:
        check_status(path, manifest, case, status, 200)
        return None
    try:
        payload = json.loads(response.decode("utf-8"))
        session_id = payload["sessionId"]
        file_token = payload["files"][file_id]
        if not isinstance(session_id, str) or not session_id or not isinstance(file_token, str) or not file_token:
            raise ValueError
    except (KeyError, TypeError, ValueError, UnicodeError):
        fail(path, manifest, case, "prepare response is missing sessionId or files[fileId]")
        return None
    print(f"[PASS] {case}: HTTP 200 with sessionId and file token")
    return session_id, file_token


def localsend_upload_target(session_id, file_id, token):
    return localsend_target(
        "/api/localsend/v2/upload",
        {"sessionId": session_id, "fileId": file_id, "token": token},
    )


def localsend_file_case(path, manifest, args, case, file_id, filename, size, sha256,
                        body, chunked=False, source=None, slow=False):
    expected_path = "/received/" + filename
    prepared = prepare_one(path, manifest, args.host, args.port, args.timeout,
                           case + "-prepare", file_id, filename, size)
    if prepared is None:
        return
    session_id, token = prepared
    target = localsend_upload_target(session_id, file_id, token)
    try:
        headers = {"Content-Type": "application/octet-stream"}
        if chunked:
            chunks = source if source is not None else small_source(body)
            status, _response, duration = http_request(
                args.host, args.port, "POST", target, chunks,
                headers, args.timeout, encode_chunked=True,
            )
        else:
            payload = source if source is not None else small_source(body)
            headers["Content-Length"] = str(size)
            status, _response, duration = http_request(
                args.host, args.port, "POST", target, payload, headers, args.timeout
            )
    except Exception as error:
        status, duration = 0, 0.0
        fail(path, manifest, case, f"upload transport failed ({type(error).__name__})")
    finally:
        cancel_session(args.host, args.port, args.timeout, session_id)
    if check_status(path, manifest, case, status, 200):
        append_item(path, manifest, case, expected_path, size, sha256, status,
                    duration, "present", kind="localsend")
    else:
        append_item(path, manifest, case, expected_path, size, sha256, status,
                    duration, "absent", kind="localsend")
    if slow and duration < 120:
        fail(path, manifest, case, "request did not progress for over 120 seconds")


def localsend_raw_upload(path, manifest, args, case, frame_name, wire_body,
                         expected_size, expected_bytes):
    filename = f"{manifest['run_prefix']}-{frame_name}.txt"
    expected_path = "/received/" + filename
    file_id = f"{manifest['run_prefix'][-8:]}-{frame_name}"
    prepared = prepare_one(path, manifest, args.host, args.port, args.timeout,
                           case + "-prepare", file_id, filename, expected_size)
    if prepared is None:
        return
    session_id, token = prepared
    target = localsend_upload_target(session_id, file_id, token)
    headers = {"Content-Type": "application/octet-stream", "Transfer-Encoding": "chunked"}
    try:
        status, _response, duration = raw_exchange(
            args.host, args.port, "POST", target, headers, (wire_body,), args.timeout,
            half_close=True,
        )
    except Exception as error:
        status, duration = 0, 0.0
        fail(path, manifest, case, f"raw upload transport failed ({type(error).__name__})")
    finally:
        cancel_session(args.host, args.port, args.timeout, session_id)
    accepted = case.endswith("valid-chunked")
    append_item(path, manifest, case, expected_path, expected_size,
                digest_bytes(expected_bytes), status, duration,
                "present" if accepted else "absent", kind="localsend")
    if accepted:
        check_status(path, manifest, case, status, 200)
    else:
        check_non_success(path, manifest, case, status)


def control_test(path, manifest, args, case, body, expected_status=None,
                 fragmented=False, declared=None, stall=0.0, half_close=False):
    if fragmented or declared is not None or stall or half_close:
        status, _response, duration = raw_prepare(args.host, args.port, args.timeout, body,
                                                  declared, fragmented, stall, half_close)
    else:
        status, _response, duration = prepare_json(args.host, args.port, args.timeout, body)
    record_prepare_case(path, manifest, case, body, status, duration)
    if expected_status is not None:
        check_status(path, manifest, case, status, expected_status)
    else:
        check_non_success(path, manifest, case, status)
    return status


def run_localsend(args):
    manifest_path = pathlib.Path(args.manifest)
    sample = pathlib.Path(args.sample)
    if not sample.is_file():
        raise RunnerError("sample file does not exist")
    sample_size, sample_hash = hash_file(sample)
    if args.slow_over_120 and sample_size < 240:
        raise RunnerError("--slow-over-120 requires a sample of at least 240 bytes")
    manifest = load_manifest(manifest_path, args.host)
    prefix = manifest["run_prefix"]

    for index in range(1, args.repeat + 1):
        filename = f"{prefix}-localsend-sample-{index:03d}.fb2"
        file_id = f"{prefix[-8:]}-sample-{index:03d}"
        localsend_file_case(manifest_path, manifest, args, f"localsend-sample-{index}",
                            file_id, filename, sample_size, sample_hash, b"",
                            source=file_chunks(sample))

    filename = f"{prefix}-localsend-chunked.fb2"
    localsend_file_case(manifest_path, manifest, args, "localsend-sample-chunked",
                        f"{prefix[-8:]}-chunked", filename, sample_size, sample_hash, b"",
                        chunked=True, source=file_chunks(sample))

    if args.slow_over_120:
        chunk_size = max(1, sample_size // 240)
        filename = f"{prefix}-localsend-slow.fb2"
        localsend_file_case(manifest_path, manifest, args, "localsend-slow-over-120",
                            f"{prefix[-8:]}-slow", filename, sample_size, sample_hash, b"",
                            source=file_chunks(sample, chunk_size, 0.53), slow=True)

    for name, wire, expected, expected_size in LOCAL_SEND_FRAMES:
        localsend_raw_upload(manifest_path, manifest, args, f"localsend-frame-{name}",
                             name, wire, expected_size, expected)

    single_id = f"{prefix[-8:]}-single"
    single_name = f"{prefix}-single.txt"
    prepared = prepare_one(manifest_path, manifest, args.host, args.port, args.timeout,
                           "localsend-prepare-single", single_id, single_name, 0)
    if prepared:
        cancel_session(args.host, args.port, args.timeout, prepared[0])


    files16 = {
        f"{prefix[-8:]}-f{index:02d}": {
            "id": f"{prefix[-8:]}-f{index:02d}",
            "fileName": f"{prefix}-16-{index:02d}.txt",
            "size": 0,
        }
        for index in range(16)
    }
    body16 = json.dumps({"files": files16}, separators=(",", ":")).encode("utf-8")
    status, response, duration = prepare_json(args.host, args.port, args.timeout, body16)
    record_prepare_case(manifest_path, manifest, "localsend-prepare-16", body16, status, duration)
    try:
        result = json.loads(response.decode("utf-8")) if status == 200 else {}
        accepted = result.get("files", {})
        session_id = result.get("sessionId")
        valid = (
            isinstance(accepted, dict)
            and accepted.keys() == files16.keys()
            and all(isinstance(token, str) and token for token in accepted.values())
            and isinstance(session_id, str)
            and bool(session_id)
        )
    except (ValueError, AttributeError, TypeError, UnicodeError):
        accepted, session_id, valid = {}, None, False
    if status != 200 or not valid:
        fail(manifest_path, manifest, "localsend-prepare-16", "expected 16 accepted file tokens")
    else:
        print("[PASS] localsend-prepare-16: accepted 16 files")
    cancel_session(args.host, args.port, args.timeout, session_id)


    exact_body = padded_prepare_body(
        make_prepare_body(f"{prefix[-8:]}-exact", f"{prefix}-exact.txt", 0), 16384
    )
    status, response, duration = prepare_json(args.host, args.port, args.timeout, exact_body)
    record_prepare_case(manifest_path, manifest, "localsend-prepare-16384", exact_body, status, duration)
    if status == 200:
        try:
            session_id = json.loads(response.decode("utf-8"))["sessionId"]
            if not isinstance(session_id, str) or not session_id:
                raise ValueError
            cancel_session(args.host, args.port, args.timeout, session_id)
            print("[PASS] localsend-prepare-16384: accepted exact 16384-byte JSON")
        except (ValueError, KeyError, TypeError, UnicodeError):
            fail(manifest_path, manifest, "localsend-prepare-16384", "prepare response has no sessionId")
    else:
        check_status(manifest_path, manifest, "localsend-prepare-16384", status, 200)

    too_large = padded_prepare_body(
        make_prepare_body(f"{prefix[-8:]}-large", f"{prefix}-large.txt", 0), 16385
    )
    control_test(manifest_path, manifest, args, "localsend-prepare-16385", too_large,
                 expected_status=400)

    fragmented = make_prepare_body(f"{prefix[-8:]}-fragment", f"{prefix}-fragment.txt", 0)
    status, response, duration = raw_prepare(args.host, args.port, args.timeout,
                                             fragmented, fragments=True)
    record_prepare_case(manifest_path, manifest, "localsend-prepare-fragmented", fragmented,
                        status, duration)
    if status == 200:
        try:
            session_id = json.loads(raw_response_body(response).decode("utf-8"))["sessionId"]
            if not isinstance(session_id, str) or not session_id:
                raise ValueError
            cancel_session(args.host, args.port, args.timeout, session_id)
            print("[PASS] localsend-prepare-fragmented: accepted fragmented JSON")
        except (ValueError, KeyError, TypeError, UnicodeError):
            fail(manifest_path, manifest, "localsend-prepare-fragmented",
                 "prepare response has no sessionId")
    else:
        check_status(manifest_path, manifest, "localsend-prepare-fragmented", status, 200)


    truncated = make_prepare_body(f"{prefix[-8:]}-disconnect", f"{prefix}-disconnect.txt", 0)
    control_test(manifest_path, manifest, args, "localsend-prepare-premature-disconnect",
                 truncated[:len(truncated) // 2], declared=len(truncated), half_close=True)
    padding = make_prepare_body(f"{prefix[-8:]}-padding", f"{prefix}-padding.txt", 0) + b" " * 4
    control_test(manifest_path, manifest, args, "localsend-prepare-truncated-padding",
                 padding, declared=len(padding) + 8, half_close=True)
    trailing = make_prepare_body(f"{prefix[-8:]}-trailing", f"{prefix}-trailing.txt", 0) + b"X"
    control_test(manifest_path, manifest, args, "localsend-prepare-trailing-data", trailing)

    empty_files = b'{"files":{}}'
    control_test(manifest_path, manifest, args, "localsend-prepare-empty-files",
                 empty_files, expected_status=204)
    deep = (
        b'{"files":{"' + prefix[-8:].encode("ascii") + b'-deep":{"fileName":"' +
        prefix.encode("ascii") + b'-deep.txt","size":0}},"ignored":' +
        b"[" * 24 + b"0" + b"]" * 24 + b"}"
    )
    control_test(manifest_path, manifest, args, "localsend-prepare-deep-nesting", deep)
    stalled = make_prepare_body(f"{prefix[-8:]}-stall", f"{prefix}-stall.txt", 0)
    control_test(manifest_path, manifest, args, "localsend-prepare-stalled", stalled[:12],
                 declared=len(stalled), stall=3.2)
    return manifest_failures(manifest)


def verify_manifest_paths(manifest):
    prefix = manifest["run_prefix"]
    if not valid_prefix(prefix):
        raise RunnerError("manifest run prefix is invalid")
    web_dir = "/" + prefix
    if manifest.get("web_dir") not in (None, web_dir):
        raise RunnerError("manifest web directory is unsafe")
    for directory in manifest.get("created_dirs", []):
        if directory != web_dir:
            raise RunnerError("manifest contains a directory outside the run")
    for item in manifest.get("items", []):
        if not isinstance(item, dict):
            raise RunnerError("manifest contains an invalid item")
        expected = item.get("expected")
        if expected not in ("present", "absent", "control"):
            raise RunnerError("manifest contains an invalid expected result")
        expected_path = item.get("path")
        if expected_path is None:
            if expected != "control":
                raise RunnerError("manifest output path is missing")
            continue
        if expected == "control" or not safe_file_path(manifest, item.get("kind"), expected_path):
            raise RunnerError("manifest contains an unsafe or tampered output path")
        if not isinstance(item.get("size"), int) or item["size"] < 0:
            raise RunnerError("manifest contains an invalid size")
        digest = item.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest):
            raise RunnerError("manifest contains an invalid SHA-256")


def run_verify(args):
    manifest_path = pathlib.Path(args.manifest)
    manifest = load_manifest(manifest_path, args.host, create=False)
    verify_manifest_paths(manifest)
    result = manifest_failures(manifest)
    seen_present = set()
    seen_absent = set()
    for item in manifest["items"]:
        expected_path = item.get("path")
        if expected_path is None:
            continue
        if item.get("expected") == "present":
            if expected_path in seen_present:
                continue
            seen_present.add(expected_path)
            case = "verify:" + item["case"]
            try:
                status, size, digest, _duration = download_info(
                    args.host, args.port, expected_path, args.timeout
                )
            except Exception as error:
                fail(manifest_path, manifest, case, f"download failed ({type(error).__name__})")
                result = 1
                continue
            if status != 200 or size != item["size"] or digest != item["sha256"]:
                fail(manifest_path, manifest, case, "downloaded bytes do not match manifest")
                result = 1
            else:
                print(f"[PASS] {case}: size and SHA-256 match")
        elif item.get("expected") == "absent" and not item.get("absence_checked"):
            if expected_path in seen_absent or expected_path in seen_present:
                continue
            seen_absent.add(expected_path)
            case = "verify:" + item["case"]
            try:
                status, _size, _digest, _duration = download_info(
                    args.host, args.port, expected_path, args.timeout
                )
            except Exception as error:
                fail(manifest_path, manifest, case, f"absence check failed ({type(error).__name__})")
                result = 1
                continue
            if status != 404:
                fail(manifest_path, manifest, case, f"expected absence, received HTTP {status}")
                result = 1
            else:
                print(f"[PASS] {case}: output is absent")
    return result


def cleanup_paths(manifest):
    verify_manifest_paths(manifest)
    files = []
    for item in manifest["items"]:
        expected_path = item.get("path")
        if expected_path and expected_path not in files:
            files.append(expected_path)
    return files


def run_cleanup(args):
    manifest_path = pathlib.Path(args.manifest)
    manifest = load_manifest(manifest_path, args.host, create=False)
    files = cleanup_paths(manifest)
    for path in files:
        target = "/delete?" + urllib.parse.urlencode({"path": path, "type": "file"})
        try:
            status, _body, _duration = http_request(args.host, args.port, "POST", target,
                                                    b"", {"Content-Length": "0"}, args.timeout)
        except Exception as error:
            print(f"[FAIL] cleanup {path}: transport failed ({type(error).__name__})")
            return 1
        if status not in (200, 404):
            print(f"[FAIL] cleanup {path}: unexpected HTTP {status}")
            return 1
        print(f"[PASS] cleanup {path}: {'deleted' if status == 200 else 'already absent'}")
    for directory in manifest.get("created_dirs", []):
        if directory != "/" + manifest["run_prefix"]:
            raise RunnerError("manifest contains an unsafe directory")
        target = "/delete?" + urllib.parse.urlencode({"path": directory, "type": "folder"})
        status, _body, _duration = http_request(args.host, args.port, "POST", target,
                                                b"", {"Content-Length": "0"}, args.timeout)
        if status not in (200, 404):
            print(f"[FAIL] cleanup run directory: unexpected HTTP {status}")
            return 1
        print(f"[PASS] cleanup {directory}: {'deleted' if status == 200 else 'already absent'}")
    return 1 if manifest["failures"] else 0


def add_network_options(parser, port):
    parser.add_argument("--host", required=True, help="device IPv4 address or hostname")
    parser.add_argument("--port", type=int, default=port, help=f"HTTP port (default: {port})")
    parser.add_argument("--manifest", required=True, help="local run manifest; it stores no session tokens")
    parser.add_argument("--timeout", type=float, default=15.0, help="finite socket timeout in seconds")


def make_parser():
    parser = argparse.ArgumentParser(
        description="Opt-in hardware upload regression runner. This sends real HTTP traffic to the target device.",
        epilog=(
            "Manual-only case: use the device UI to abort an active upload with Back/Power, then verify its partial "
            "file is absent. This runner cannot simulate or claim a physical UI abort. Example: python3 "
            "test/scripts/UploadSmoke.py web --host 192.168.8.181 --sample /tmp/papyrix-issue-168.fb2 "
            "--manifest /tmp/papyrix-upload-run.json"
        ),
    )
    commands = parser.add_subparsers(dest="command", required=True)
    manual_abort = (
        "Manual only: abort an active Web upload with Back/Power, or abort a LocalSend receive in its UI. "
        "Check that the partial file is absent. This runner does not simulate these UI actions."
    )
    web = commands.add_parser("web", help="exercise web multipart uploads", epilog=manual_abort)
    add_network_options(web, WEB_PORT)
    web.add_argument("--sample", required=True, help="large local sample file (streamed)")
    web.add_argument("--repeat", type=int, default=1, help="number of sample upload repetitions")
    web.add_argument("--slow-over-120", action="store_true",
                     help="stream one progressing sample upload for more than 120 seconds")
    web.set_defaults(run=run_web)

    localsend = commands.add_parser("localsend", help="exercise LocalSend prepare and upload framing",
                                   epilog=manual_abort)
    add_network_options(localsend, LOCALSEND_PORT)
    localsend.add_argument("--sample", required=True, help="large local sample file (streamed)")
    localsend.add_argument("--repeat", type=int, default=1, help="number of sample upload repetitions")
    localsend.add_argument("--slow-over-120", action="store_true",
                           help="stream one progressing sample upload for more than 120 seconds")
    localsend.set_defaults(run=run_localsend)

    verify = commands.add_parser("verify", help="download and verify recorded outputs after switching to web mode")
    add_network_options(verify, WEB_PORT)
    verify.set_defaults(run=run_verify)

    cleanup = commands.add_parser("cleanup", help="delete only run-prefixed manifest-owned output files")
    add_network_options(cleanup, WEB_PORT)
    cleanup.set_defaults(run=run_cleanup)
    return parser


def main():
    parser = make_parser()
    args = parser.parse_args()
    if args.port < 1 or args.port > 65535:
        parser.error("--port must be in 1..65535")
    if not (0 < args.timeout <= 3600):
        parser.error("--timeout must be finite and in 0..3600 seconds")
    if args.command in ("web", "localsend") and args.repeat < 1:
        parser.error("--repeat must be at least 1")
    try:
        result = args.run(args)
    except (RunnerError, OSError, http.client.HTTPException, ValueError) as error:
        print(f"[FAIL] {error}")
        return 1
    return result


if __name__ == "__main__":
    raise SystemExit(main())
