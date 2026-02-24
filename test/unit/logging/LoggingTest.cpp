#include <cstdio>
#include <cstring>
#include <string>

// Local HardwareSerial.h mock (resolved before test/mocks/ via -I order)
// provides HWCDC with output capture. Must define globals BEFORE Logging.h
// redefines Serial to MySerialImpl::instance.
#include "HardwareSerial.h"

CaptureState captureState;
HWCDC Serial;

static unsigned long millisValue = 0;
unsigned long millis() { return millisValue; }

// ENABLE_SERIAL_LOG and LOG_LEVEL=2 are set via CMake compile definitions.
// Include the real Logging.h (resolved before test/mocks/ via BEFORE include dirs).
#include <Logging.h>

// Simple inline test helpers (can't use test_utils — it pulls in platform_stubs
// which conflicts with our HWCDC mock and redefines LOG_* / logSerial)
static int passCount = 0;
static int failCount = 0;

static void pass(const char* name) {
  printf("  \xE2\x9C\x93 PASS: %s\n", name);
  passCount++;
}

static void fail(const char* name, const char* detail) {
  fprintf(stderr, "  \xE2\x9C\x97 FAIL: %s\n", name);
  if (detail) fprintf(stderr, "    %s\n", detail);
  failCount++;
}

static void expectEq(const std::string& expected, const std::string& actual, const char* name) {
  if (expected == actual) {
    pass(name);
  } else {
    fail(name, nullptr);
    fprintf(stderr, "    Expected: \"%s\"\n", expected.c_str());
    fprintf(stderr, "    Actual:   \"%s\"\n", actual.c_str());
  }
}

static void expectContains(const std::string& haystack, const std::string& needle, const char* name) {
  if (haystack.find(needle) != std::string::npos) {
    pass(name);
  } else {
    fail(name, nullptr);
    fprintf(stderr, "    Expected to contain: \"%s\"\n", needle.c_str());
    fprintf(stderr, "    Actual: \"%s\"\n", haystack.c_str());
  }
}

static void expectTrue(bool cond, const char* name) {
  if (cond) {
    pass(name);
  } else {
    fail(name, "condition was false");
  }
}

static void reset() {
  captureState.output.clear();
  captureState.enabled = true;
  millisValue = 0;
}

int main() {
  printf("\n========================================\n");
  printf("Test Suite: Logging Tests\n");
  printf("========================================\n");

  // --- logPrintf basic format ---
  {
    reset();
    millisValue = 42;
    logPrintf("[INF]", "TEST", "hello %d\n", 123);
    expectEq("[42] [INF] [TEST] hello 123\n", captureState.output, "logPrintf: basic format");
  }

  // --- logPrintf zero millis ---
  {
    reset();
    millisValue = 0;
    logPrintf("[ERR]", "X", "msg\n");
    expectEq("[0] [ERR] [X] msg\n", captureState.output, "logPrintf: zero millis");
  }

  // --- logPrintf no format args ---
  {
    reset();
    millisValue = 1;
    logPrintf("[DBG]", "A", "plain text\n");
    expectEq("[1] [DBG] [A] plain text\n", captureState.output, "logPrintf: no format args");
  }

  // --- LOG_ERR macro ---
  {
    reset();
    millisValue = 100;
    LOG_ERR("MOD", "error %s", "msg");
    expectEq("[100] [ERR] [MOD] error msg\n", captureState.output, "LOG_ERR macro");
  }

  // --- LOG_INF macro ---
  {
    reset();
    millisValue = 200;
    LOG_INF("NET", "connected to %s port %d", "host", 8080);
    expectEq("[200] [INF] [NET] connected to host port 8080\n", captureState.output, "LOG_INF macro");
  }

  // --- LOG_DBG macro ---
  {
    reset();
    millisValue = 300;
    LOG_DBG("GFX", "render took %lu ms", 42UL);
    expectEq("[300] [DBG] [GFX] render took 42 ms\n", captureState.output, "LOG_DBG macro");
  }

  // --- Serial disabled produces no output ---
  {
    reset();
    captureState.enabled = false;
    logPrintf("[INF]", "TEST", "should not appear\n");
    expectTrue(captureState.output.empty(), "serial disabled: no output");
  }

  // --- Long origin truncated, no crash ---
  {
    reset();
    millisValue = 1;
    char longOrigin[300];
    memset(longOrigin, 'A', 299);
    longOrigin[299] = '\0';
    logPrintf("[INF]", longOrigin, "end\n");
    // Must not crash; output truncated to fit 256-byte buffer
    expectTrue(!captureState.output.empty(), "long origin: produces output");
    expectTrue(captureState.output.size() <= 256, "long origin: output within buffer limit");
    expectContains(captureState.output, "[1] [INF]", "long origin: has prefix");
  }

  // --- Long message truncated, no crash ---
  {
    reset();
    millisValue = 1;
    char longMsg[500];
    memset(longMsg, 'X', 499);
    longMsg[499] = '\0';
    logPrintf("[ERR]", "T", "%s\n", longMsg);
    expectTrue(!captureState.output.empty(), "long message: produces output");
    expectTrue(captureState.output.size() <= 256, "long message: output within buffer limit");
    expectContains(captureState.output, "[1] [ERR] [T]", "long message: has prefix");
  }

  // --- Empty format string ---
  {
    reset();
    millisValue = 5;
    logPrintf("[DBG]", "Z", "");
    expectEq("[5] [DBG] [Z] ", captureState.output, "empty format: just prefix");
  }

  // --- MySerialImpl deprecated printf wrapper ---
  {
    reset();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    // After #define Serial, 'Serial' is MySerialImpl::instance.
    // MySerialImpl::printf forwards to logSerial.print() which captures output.
    Serial.printf("direct %d\n", 99);
#pragma GCC diagnostic pop
    expectEq("direct 99\n", captureState.output, "MySerialImpl::printf wrapper");
  }

  // --- Print summary ---
  printf("\n========================================\n");
  printf("Test Suite: Logging Tests - Summary\n");
  printf("========================================\n");
  printf("Total tests: %d\n", passCount + failCount);
  printf("  Passed: %d\n", passCount);
  printf("  Failed: %d\n", failCount);
  printf("\n%s\n", failCount == 0 ? "\xE2\x9C\x93 ALL TESTS PASSED" : "\xE2\x9C\x97 SOME TESTS FAILED");
  printf("========================================\n");

  return failCount > 0 ? 1 : 0;
}
