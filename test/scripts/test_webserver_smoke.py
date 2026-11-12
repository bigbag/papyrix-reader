#!/usr/bin/env python3
"""Check smoke-test rejection and cleanup through a local HTTP server."""

import contextlib
import http.client
import importlib.util
import io
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import threading
import unittest
from unittest.mock import patch
import urllib.parse


SPEC = importlib.util.spec_from_file_location("webserver_smoke", Path(__file__).with_name("WebServerSmoke.py"))
SMOKE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SMOKE)


class UploadClock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class SmokeEndpoint(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def respond(self, status, body):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def do_GET(self):
        if self.path.startswith("/api/files?"):
            if self.server.fail_listing and self.server.uploaded:
                self.respond(200, b"not-json")
            else:
                files = [{"name": name, "isDirectory": False, "size": len(data)}
                         for name, data in self.server.files.items()]
                self.respond(200, json.dumps(files).encode())
        elif self.path.startswith("/download?"):
            if self.server.drop_download:
                self.close_connection = True
                return
            name = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)["path"][0].lstrip("/")
            self.respond(200, self.server.files[name])
        else:
            self.respond(200, json.dumps({
                "version": "test", "ip": "127.0.0.1", "mode": "AP",
                "rssi": 0, "freeHeap": 65536, "uptime": 100,
            }).encode())

    def do_POST(self):
        size = int(self.headers["Content-Length"])
        if self.path.startswith("/delete?"):
            if self.server.fail_delete:
                self.respond(503, b"Delete failed")
                return
            name = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)["path"][0].lstrip("/")
            self.server.files.pop(name, None)
            self.respond(200, b"OK")
            return
        if self.server.post_status is None:
            body = self.rfile.read(size)
            name = re.search(rb'filename="([^"]+)"', body).group(1).decode()
            data = body.split(b"\r\n\r\n", 1)[1].rsplit(b"\r\n--", 1)[0]
            self.server.files[name] = data
            self.server.uploaded = True
            self.respond(200, b"OK")
            return
        if size == 100000:
            self.rfile.read1(size)
        else:
            self.rfile.read(size)
            if self.server.reject_complete_forms:
                self.close_connection = True
                return
        self.respond(self.server.post_status, b"No upload performed")


class WebServerSmokeTest(unittest.TestCase):
    def setUp(self):
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), SmokeEndpoint)
        self.server.post_status = 400
        self.server.files = {}
        self.server.uploaded = False
        self.server.fail_listing = False
        self.server.drop_download = False
        self.server.fail_delete = False
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server.reject_complete_forms = False
        self.thread.start()
        self.addCleanup(self.thread.join)
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def test_multipart_check_rejects_route_responses(self):
        for status in (200, 400):
            with self.subTest(status=status):
                self.server.post_status = status
                with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(AssertionError):
                    SMOKE.malformed_multipart("127.0.0.1", self.server.server_port, {"uptime": 1})

    def test_streaming_multipart_check_rejects_route_response(self):
        self.server.reject_complete_forms = True
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(AssertionError):
            SMOKE.malformed_multipart("127.0.0.1", self.server.server_port, {"uptime": 1})

    def check_upload(self):
        self.server.post_status = None
        with patch.object(SMOKE, "time", UploadClock()):
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                SMOKE.upload_check("127.0.0.1", self.server.server_port)

    def test_listing_failure_removes_uploaded_file(self):
        self.server.fail_listing = True
        with self.assertRaises(json.JSONDecodeError):
            self.check_upload()
        self.assertEqual([], list(self.server.files))

    def test_cleanup_error_preserves_download_disconnect(self):
        self.server.drop_download = True
        self.server.fail_delete = True
        with self.assertRaises(Exception) as raised:
            self.check_upload()
        self.assertIsInstance(raised.exception, http.client.RemoteDisconnected)

    def test_cleanup_error_fails_successful_transfer(self):
        self.server.fail_delete = True
        with self.assertRaises(AssertionError):
            self.check_upload()


if __name__ == "__main__":
    unittest.main()
