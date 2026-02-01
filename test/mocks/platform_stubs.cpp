#include "platform_stubs.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

#include "WString.h"

// Global mock instances
MockSerial Serial;
MockSPI SPI;
MockESP ESP;

void MockSerial::printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void MockSerial::println(const char* s) {
  if (s)
    ::printf("%s\n", s);
  else
    ::printf("\n");
}

void MockSerial::println() { ::printf("\n"); }

void MockSerial::print(const char* s) {
  if (s) ::printf("%s", s);
}

void MockSerial::println(int v) { ::printf("%d\n", v); }

void MockSerial::println(unsigned long v) { ::printf("%lu\n", v); }

void MockSerial::print(int v) { ::printf("%d", v); }

void MockSerial::println(const String& s) {
  if (s.c_str())
    ::printf("%s\n", s.c_str());
  else
    ::printf("\n");
}

void MockSerial::print(const String& s) {
  if (s.c_str()) ::printf("%s", s.c_str());
}

unsigned long millis() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}
