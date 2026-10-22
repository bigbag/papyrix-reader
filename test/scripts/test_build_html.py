#!/usr/bin/env python3
"""Check generated web assets and conditional page responses."""

import gzip
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from test_reader_resume import block

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "build_html.py"

HARNESS = r'''
#include <cassert>
#include <map>
#include <string>
#include <string_view>
#include "WString.h"
#define PROGMEM
#include "AppPageHtml.generated.h"

class WebServer {
 public:
  std::string condition;
  std::map<std::string, std::string> headers;
  std::string body;
  size_t contentLength = 0;
  int status = 0;
  String header(const char* name) const {
    return std::string(name) == "If-None-Match" ? String(condition) : String();
  }
  void sendHeader(const char* name, const char* value) { headers[name] = value; }
  void setContentLength(size_t length) { contentLength = length; }
  void send(int code, const char*, const char* content) {
    status = code;
    body = content;
  }
  void send_P(int code, const char*, const char* data, size_t length) {
    status = code;
    body.assign(data, length);
    contentLength = length;
  }
};

@production@

void check(const std::string& condition, int status) {
  WebServer server;
  server.condition = condition;
  sendGzipHtml(&server, AppPageHtml, AppPageHtmlCompressedSize, AppPageHtmlETag);
  assert(server.status == status);
  assert(server.headers.at("ETag") == AppPageHtmlETag);
  assert(server.headers.at("Cache-Control") == "no-cache");
  assert(server.headers.at("Content-Encoding") == "gzip");
  assert(server.contentLength == AppPageHtmlCompressedSize);
  if (status == 304) {
    assert(server.body.empty());
  } else {
    assert(server.body == std::string(AppPageHtml, AppPageHtmlCompressedSize));
  }
}

int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string tag = AppPageHtmlETag;
  assert(tag == argv[2]);
  check("", 200);
  check("\"stale\"", 200);
  check("W/\"stale\"", 200);
  check(argv[1], 200);
  check(tag, 304);
  check("W/" + tag, 304);
  check(" \t" + tag + "\t ", 304);
  check("\"old\", W/" + tag + ", \"other\"", 304);
  check("\"old,with,commas\", " + tag, 304);
  check(tag + ", W/\"other\"", 304);
  check(", , " + tag + ", ", 304);
  check("*", 304);
  check(" \t* \t", 304);
  check("\"old\", \"other\"", 200);
  check(" \t, ,", 200);
  check(tag.substr(1, tag.size() - 2), 200);
  check(tag.substr(0, tag.size() - 1), 200);
  check(tag + "extra", 200);
  check(tag + ", broken", 200);
  check(tag + ", *", 200);
  check("*, " + tag, 200);
  check("W/", 200);
  check("w/" + tag, 200);
  check("\"" + tag.substr(1, tag.size() - 2) + "0\"", 200);
  check("\"prefix" + tag.substr(1), 200);
}
'''


def generate(directory, html):
    source = directory / "src" / "AppPage.html"
    source.parent.mkdir(exist_ok=True)
    source.write_text(html, encoding="utf-8")
    subprocess.run([sys.executable, str(SCRIPT)], cwd=directory, check=True, capture_output=True)
    header = source.with_name("AppPageHtml.generated.h").read_text()
    compressed = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", header))
    assert gzip.decompress(compressed).decode("utf-8") == html
    return header, compressed


with tempfile.TemporaryDirectory(prefix="papyrix-web-cache-") as directory:
    path = Path(directory)
    first_header, first = generate(path, "<html><body>test</body></html>")
    second_header, second = generate(path, "<html><body>test</body></html>")
    assert first_header == second_header
    assert first == second
    assert first[4:8] == b"\x00\x00\x00\x00"
    _, changed = generate(path, "<html><body>next</body></html>")
    assert first != changed

    source = (ROOT / "src/network/PapyrixWebServer.cpp").read_text()
    production = "\n".join(block(source, marker) for marker in
                           ("static bool matchesEtag(", "static void sendGzipHtml("))
    program = path / "web_cache.cpp"
    program.write_text(HARNESS.replace("@production@", production))
    binary = path / "web_cache"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-narrowing",
                    "-I", str(ROOT / "test/mocks"), "-I", str(path / "src"),
                    str(program), "-o", str(binary)], check=True)
    old_tag = '"' + hashlib.sha256(first).hexdigest() + '"'
    expected_tag = '"' + hashlib.sha256(changed).hexdigest() + '"'
    subprocess.run([str(binary), old_tag, expected_tag], check=True)

print("PASS: stable web assets, content-based ETags, and conditional page responses")
