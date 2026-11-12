#!/usr/bin/env python3
"""Check web directory responses under JSON allocation pressure."""

import json
import subprocess
import tempfile
from pathlib import Path

from test_reader_resume import block

ROOT = Path(__file__).resolve().parents[2]
ARDUINOJSON = ROOT / ".pio/libdeps/default/ArduinoJson"
if not (ARDUINOJSON / "src/ArduinoJson.h").is_file():
    raise SystemExit("Run 'pio pkg install -e default' to provide ArduinoJson")

HARNESS = r'''
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include "WebFileNameValidation.h"

#undef LOG_ERR
#define LOG_ERR(...) ((void)0)
constexpr size_t CONTENT_LENGTH_UNKNOWN = static_cast<size_t>(-1);
MockSerial Serial;

class FailingAllocator : public ArduinoJson::Allocator {
 public:
  int failAt = 0;
  int calls = 0;
  void* allocate(size_t size) override {
    return ++calls == failAt || failAt < 0 ? nullptr : std::malloc(size);
  }
  void deallocate(void* ptr) override { std::free(ptr); }
  void* reallocate(void* ptr, size_t size) override {
    return ++calls == failAt || failAt < 0 ? nullptr : std::realloc(ptr, size);
  }
} allocator;

class TestJsonDocument : public ArduinoJson::JsonDocument {
 public:
  TestJsonDocument() : ArduinoJson::JsonDocument(&::allocator) {}
};

class WebServer {
 public:
  struct Client { void setNoDelay(bool) {} void stop() {} } socket;
  int status = 0;
  std::string path;
  bool hasPath = false;
  std::string body;
  bool hasArg(const char*) const { return hasPath; }
  String arg(const char*) const { return String(path); }
  Client& client() { return socket; }
  void setContentLength(size_t) {}
  void send(int code, const char*, const char* value) {
    if (!status) { status = code; body = value; }
  }
  void sendContent(const char* value) { body += value; }
  void sendContent(const char* value, size_t length) { body.append(value, length); }
};

class PapyrixWebServer {
 public:
  explicit PapyrixWebServer(WebServer* server) : server_(server) {}
  void handleFileListData();
 private:
  WebServer* server_;
};

namespace web = papyrix::web;
@PATH_VALIDATION@
@PREPARE_PATH@
#define JsonDocument TestJsonDocument
@HANDLER@
#undef JsonDocument

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  allocator.failAt = std::atoi(argv[1]);
  const std::string longName = std::string(250, 'a') + ".epub";
  const std::string escaped = std::string(240, '\\') + ".txt";
  const std::string quotes(255, '"');
  SdMan.registerDirectory("/", {{"Books", true}, {"Novel.EPUB", false},
                              {"a\"b\\c.txt", false}, {"caf\xc3\xa9.epub", false},
                              {longName, false}, {escaped, false}, {quotes, false},
                              {".secret", false}, {"config", true}});
  SdMan.registerDirectory("/Books", {});
  SdMan.registerFile("/Novel.EPUB", "1234567");
  SdMan.registerFile("/a\"b\\c.txt", "text");
  SdMan.registerFile("/caf\xc3\xa9.epub", "abc");
  SdMan.registerFile("/" + longName, "long");
  SdMan.registerFile("/" + escaped, "escape");
  SdMan.registerFile("/" + quotes, "quotes");
  WebServer server;
  const std::string scenario = argv[2];
  if (scenario != "root") {
    server.hasPath = true;
    server.path = scenario == "bad" ? "/../secret" : "/missing";
  }
  PapyrixWebServer app(&server);
  app.handleFileListData();
  std::cout << server.status << '\n' << server.body;
}
'''

source = (ROOT / "src/network/PapyrixWebServer.cpp").read_text()
start = source.index("void PapyrixWebServer::handleFileListData(")
handler = source[start:source.index("\n}\n", start) + 2]
validation = (ROOT / "src/network/WebFileNameValidation.cpp").read_text()
program = (HARNESS.replace("@PATH_VALIDATION@", "namespace papyrix::web {\n" +
                           block(validation, "bool isSafeWebPath(") + "\n}")
           .replace("@PREPARE_PATH@", block(source, "bool prepareWebPath("))
           .replace("@HANDLER@", handler))

expected = [
    {"name": "Books", "isDirectory": True, "size": 0, "isEpub": False},
    {"name": "Novel.EPUB", "isDirectory": False, "size": 7, "isEpub": True},
    {"name": 'a"b\\c.txt', "isDirectory": False, "size": 4, "isEpub": False},
    {"name": "café.epub", "isDirectory": False, "size": 3, "isEpub": True},
    {"name": "a" * 250 + ".epub", "isDirectory": False, "size": 4, "isEpub": True},
    {"name": "\\" * 240 + ".txt", "isDirectory": False, "size": 6, "isEpub": False},
    {"name": '"' * 255, "isDirectory": False, "size": 6, "isEpub": False},
]

with tempfile.TemporaryDirectory(prefix="papyrix-web-list-") as directory:
    path = Path(directory)
    cpp = path / "web_list.cpp"
    cpp.write_text(program)
    binary = path / "web_list"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "test/mocks"), "-I", str(ROOT / "lib/FsHelpers/src"),
                    "-I", str(ROOT / "src/network"), "-I", str(ARDUINOJSON / "src"),
                    str(cpp), str(ROOT / "lib/FsHelpers/src/FsHelpers.cpp"),
                    "-o", str(binary)], check=True)

    for fail_at, scenario in [(1, "root"), (2, "root"), (-1, "root"), (0, "root"),
                              (0, "bad"), (0, "missing")]:
        result = subprocess.run([str(binary), str(fail_at), scenario], capture_output=True, text=True)
        raw = f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        assert result.returncode == 0, raw
        status_text, separator, body = result.stdout.partition("\n")
        assert separator and status_text.isdigit(), raw
        status = int(status_text)
        if scenario == "root" and status == 200:
            try:
                entries = json.loads(body)
            except (json.JSONDecodeError, UnicodeDecodeError) as error:
                raise AssertionError(f"Invalid list: {error}; {raw}") from error
            assert entries == expected, f"Incomplete or incorrect list (allocation {fail_at}): {raw}"
        elif scenario == "root":
            assert fail_at and 400 <= status < 600, raw
            if body.lstrip().startswith("["):
                assert json.loads(body) == [], raw
        else:
            assert status == (400 if scenario == "bad" else 404), raw
            assert json.loads(body) == [], raw
print("PASS: web directory listing and JSON allocation failures")
