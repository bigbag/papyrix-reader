#!/usr/bin/env python3
"""Exercise the patched SDK parser with host transport and allocation seams."""

import importlib.util
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PARSER = ROOT / ".pio/packages/framework-arduinoespressif32/libraries/WebServer/src/Parsing.cpp"

# The harness compiles the patched SDK functions unchanged. NetworkClient,
# millis/yield, WebServer's argument storage, and nothrow array allocation are
# host seams. The repository String mock is adjusted for substring(start, -1).
HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include "WString.h"

#define WEBSERVER_MAX_LINE_WAIT 5000
#define WEBSERVER_MAX_QUERY_ARGS 256
#define WEBSERVER_MAX_POST_ARGS 32
#define WEBSERVER_MAX_LINE_LEN 4096
#define WEBSERVER_MAX_POST_ARG_LEN 16384
#define WEBSERVER_MAX_MULTIPART_SKIP_LINES 64
#define WEBSERVER_MAX_HEADER_WAIT 10000
#define HTTP_MAX_POST_WAIT 5000
#define HTTP_UPLOAD_BUFLEN 1436
#define F(value) value
#define FPSTR(value) value
#define log_e(...) ((void)0)
#define log_w(...) ((void)0)
#define log_v(...) ((void)0)

static unsigned long clock_ms = 1;
static unsigned long millis() { return clock_ms; }
static void yield() { ++clock_ms; }
static void delay(unsigned long ms) { clock_ms += ms; }

class NetworkClient {
 public:
  std::string bytes;
  size_t pos = 0;
  unsigned long byte_delay = 0;
  explicit NetworkClient(std::string input, unsigned long delay = 0)
      : bytes(std::move(input)), byte_delay(delay) {}
  unsigned long getTimeout() const { return 30000; }
  int read() {
    if (pos == bytes.size()) return -1;
    clock_ms += byte_delay;
    return static_cast<unsigned char>(bytes[pos++]);
  }
  int peek() const {
    return pos == bytes.size() ? -1 : static_cast<unsigned char>(bytes[pos]);
  }
  bool connected() const { return pos < bytes.size(); }
  int available() const { return static_cast<int>(bytes.size() - pos); }
};

static bool fail_next_array = false;
static size_t last_array_size = 0;
void* operator new[](size_t size) {
  last_array_size = size;
  if (void* p = std::malloc(size)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  last_array_size = size;
  if (fail_next_array) {
    fail_next_array = false;
    return nullptr;
  }
  return std::malloc(size);
}
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

enum UploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  UploadStatus status;
  String name, filename, type;
  size_t totalSize = 0, currentSize = 0;
  uint8_t buf[HTTP_UPLOAD_BUFLEN];
};
namespace mime {
enum { txt };
struct Mime { const char* mimeType; };
static const Mime mimeTable[] = {{"text/plain"}};
}
static const char Content_Type[] = "Content-Type";
static const char filename[] = "filename";
class WebServer;
struct RequestHandler {
  bool fail_merge = false;
  int aborts = 0;
  bool canUpload(WebServer&, const String&) { return true; }
  void upload(WebServer&, const String&, HTTPUpload&);
};

class WebServer {
 public:
  struct RequestArgument { String key; String value; RequestArgument* next; };
  int _currentArgCount = 0;
  RequestArgument* _currentArgs = nullptr;
  RequestArgument* _postArgs = nullptr;
  int _postArgsLen = 0;
  std::unique_ptr<HTTPUpload> _currentUpload;
  RequestHandler* _currentHandler = nullptr;
  String _currentUri = "/upload";
  ~WebServer() { delete[] _currentArgs; delete[] _postArgs; }
  void _parseArguments(const String& data);
  static String urlDecode(const String& text);
  bool _parseForm(NetworkClient&, const String&, uint32_t);
  bool _parseFormUploadAborted();
  int _uploadReadByte(NetworkClient&);
  void _uploadWriteByte(uint8_t);
  String argName(int) const;
  bool hasArg(const String&) const;
  String arg(const String&) const;
};

@LINE_STATUS@
@READ_LINE@
@PARSE_ARGUMENTS@
@URL_DECODE@
@PARSE_FORM@
@FORM_ABORT@
@UPLOAD_READ@
@UPLOAD_WRITE@
@ARG_NAME@
@HAS_ARG@
@ARG@

void RequestHandler::upload(WebServer& server, const String&, HTTPUpload& upload) {
  if (fail_merge && upload.status == UPLOAD_FILE_END) fail_next_array = true;
  if (upload.status == UPLOAD_FILE_ABORTED) {
    assert(server._currentArgCount == 0 || server._currentArgs != nullptr);
    assert(server.argName(0) == "");
    ++aborts;
  }
}

int main() {
  String line;
  NetworkClient normal("GET / HTTP/1.1\r\n");
  assert(readLineWithLimit(normal, line, 4096) == LineStatus::Ok);
  assert(line == "GET / HTTP/1.1");
  assert(normal.pos == normal.bytes.size());

  NetworkClient maximum(std::string(4096, 'x') + "\r\n");
  assert(readLineWithLimit(maximum, line, 4096) == LineStatus::Ok);
  assert(line.length() == 4096);
  NetworkClient over(std::string(4097, 'x') + "\r\n");
  assert(readLineWithLimit(over, line, 4096) == LineStatus::TooLong);

  clock_ms = 1;
  NetworkClient unfinished("partial");
  assert(readLineWithLimit(unfinished, line, 4096) == LineStatus::TimedOut);
  assert(clock_ms <= 5001);
  clock_ms = 1;
  NetworkClient bare_cr("header\r");
  assert(readLineWithLimit(bare_cr, line, 4096) == LineStatus::TimedOut);
  assert(clock_ms <= 5001);

  clock_ms = 1;
  const unsigned long phase_start = clock_ms;
  NetworkClient headers(std::string(100, 'a'), 100);
  headers.bytes.clear();
  for (int i = 0; i < 100; ++i) headers.bytes += "x\r\n";
  int complete = 0;
  LineStatus status = LineStatus::Ok;
  while (status == LineStatus::Ok && complete < 100) {
    status = readLineWithLimit(headers, line, 4096, phase_start, 10000);
    if (status == LineStatus::Ok) ++complete;
  }
  assert(complete > 1 && complete < 100);
  assert(status == LineStatus::TimedOut);
  assert(clock_ms - phase_start <= 10300);

  WebServer server;
  server._parseArguments("");
  assert(server._currentArgCount == 0);
  assert(server._currentArgs != nullptr);
  const size_t empty_allocation = last_array_size;
  server._parseArguments(String(std::string(4096, '&')));
  assert(server._currentArgCount == 0);
  assert(server._currentArgs != nullptr);
  assert(last_array_size == empty_allocation);

  server._parseArguments("&&ignored&first=hello+world&escaped=%2F%26&empty=&tail");
  assert(server._currentArgCount == 3);
  assert(server._currentArgs[0].key == "first");
  assert(server._currentArgs[0].value == "hello world");
  assert(server._currentArgs[1].key == "escaped");
  assert(server._currentArgs[1].value == "/&");
  assert(server._currentArgs[2].key == "empty");
  assert(server._currentArgs[2].value == "");

  std::string query;
  for (int i = 0; i < 300; ++i) {
    if (i) query += '&';
    query += "k" + std::to_string(i) + "=" + std::to_string(i);
  }
  server._parseArguments(String(query));
  assert(server._currentArgCount == 256);
  assert(server._currentArgs[0].key == "k0");
  assert(server._currentArgs[255].key == "k255");
  assert(server._currentArgs[255].value == "255");

  fail_next_array = true;
  server._parseArguments("a=1&b=2");
  assert(!fail_next_array);
  assert(server._currentArgCount == 0);
  server._parseArguments("after=ok");
  assert(server._currentArgCount == 1);
  assert(server._currentArgs[0].value == "ok");

  const std::string body = "--B\r\nContent-Disposition: form-data; name=\"file\"; filename=\"test.txt\"\r\n"
                           "Content-Type: text/plain\r\n\r\npayload\r\n--B--\r\n";
  for (bool fail : {false, true}) {
    WebServer form;
    form._parseArguments("path=%2F");
    RequestHandler handler;
    handler.fail_merge = fail;
    form._currentHandler = &handler;
    NetworkClient client(body);
    assert(form._parseForm(client, "B", body.size()) == !fail);
    assert(handler.aborts == (fail ? 1 : 0));
    if (fail) {
      assert(form._currentArgCount == 0 && form._currentArgs == nullptr);
      assert(form._postArgsLen == 0 && form._postArgs == nullptr);
    } else {
      assert(form.argName(0) == "path");
      assert(form.arg("path") == "/");
      assert(form._currentUpload->totalSize == 7);
    }
  }

  const std::string disposition = "--B\r\nContent-Disposition: form-data; name=\"field\"\r\n";
  bool metadata_unbounded = false;
  for (bool empty_field : {false, true}) {
    std::string repeated;
    for (int i = 0; i < 2000; ++i) repeated += empty_field ? "\r\n" : "X: a\r\n";
    const std::string multipart = disposition + (empty_field ? "\r\n" : "") + repeated +
                                  (empty_field ? "" : "\r\nvalue") + "\r\n--B--\r\n";
    clock_ms = 1;
    WebServer form;
    NetworkClient client(multipart, 10);
    const bool accepted = form._parseForm(client, "B", multipart.size());
    if (accepted || clock_ms > 10011) {
      std::fprintf(stderr, "%s bypasses the metadata deadline: accepted=%d, elapsed=%lu ms\n",
                   empty_field ? "Empty field stream" : "MIME header stream", accepted, clock_ms - 1);
      metadata_unbounded = true;
    }
    assert(form._postArgsLen == 0 && form._postArgs == nullptr);
  }
  assert(!metadata_unbounded);

  clock_ms = 1;
  const std::string slow_file = "--B\r\nContent-Disposition: form-data; name=\"file\"; filename=\"slow.txt\"\r\n"
                                "\r\n" + std::string(2048, 'x') + "\r\n--B\r\n"
                                "Content-Disposition: form-data; name=\"after\"\r\n\r\nok\r\n--B--\r\n";
  WebServer slow_form;
  NetworkClient slow_client(slow_file, 10);
  assert(slow_form._parseForm(slow_client, "B", slow_file.size()));
  assert(clock_ms > 10000);
  assert(slow_form._currentUpload->totalSize == 2048);
  assert(slow_form.arg("after") == "ok");
}
'''


def definition(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    pos = opening + 1
    while depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    return source[start:pos]


def main():
    if not PARSER.is_file():
        raise SystemExit(f"Missing installed Arduino ESP32 3.3.7 SDK parser: {PARSER}")
    spec = importlib.util.spec_from_file_location("patch_webserver", ROOT / "scripts/patch_webserver.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-arguments", action="store_true",
                        help="use the original SDK argument parser as a negative control")
    options = parser.parse_args()
    pristine = PARSER.read_text()
    try:
        module.patch_parser(pristine + "\n")
    except RuntimeError:
        pass
    else:
        raise AssertionError("Unknown SDK parser source is accepted")
    source = module.patch_parser(pristine)
    server = PARSER.with_name("WebServer.cpp").read_text()
    fragments = {
        "@LINE_STATUS@": definition(source, "enum class LineStatus") + ";",
        "@READ_LINE@": definition(source, "static LineStatus readLineWithLimit("),
        "@PARSE_ARGUMENTS@": definition(
            pristine if options.baseline_arguments else source, "void WebServer::_parseArguments("),
        "@URL_DECODE@": definition(source, "String WebServer::urlDecode("),
        "@PARSE_FORM@": definition(source, "bool WebServer::_parseForm("),
        "@FORM_ABORT@": definition(source, "bool WebServer::_parseFormUploadAborted("),
        "@UPLOAD_READ@": definition(source, "int WebServer::_uploadReadByte("),
        "@UPLOAD_WRITE@": definition(source, "void WebServer::_uploadWriteByte("),
        "@ARG_NAME@": definition(server, "String WebServer::argName("),
        "@HAS_ARG@": definition(server, "bool WebServer::hasArg("),
        "@ARG@": definition(server, "String WebServer::arg(const String &name)"),
    }
    harness = HARNESS
    for marker, fragment in fragments.items():
        harness = harness.replace(marker, fragment)
    mock = (ROOT / "test/mocks/WString.h").read_text()
    old = "if (end < start) end = start;"
    if old not in mock:
        raise SystemExit("String mock substring seam changed")
    mock = mock.replace(old, "if (end < 0) end = (int)s_.size();\n    " + old, 1)
    mock = mock.replace("  bool isEmpty() const", """
  bool equalsIgnoreCase(const char* other) const {
    std::string left = s_, right = other;
    for (char& c : left) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : right) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return left == right;
  }
  bool isEmpty() const""", 1)
    with tempfile.TemporaryDirectory(prefix="papyrix-webserver-bounds-") as directory:
        path = Path(directory)
        (path / "WString.h").write_text(mock)
        (path / "bounds.cpp").write_text(harness)
        executable = path / "bounds"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", str(path / "bounds.cpp"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print("PASS: WebServer line and argument bounds")


if __name__ == "__main__":
    main()
