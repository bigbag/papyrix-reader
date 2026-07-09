#pragma once

#include <cstdint>
#include <map>
#include <string>

class File {
 public:
  File() = default;
  operator bool() const { return false; }
  void close() {}
  size_t size() const { return 0; }
  int read(uint8_t*, size_t) { return -1; }
};

class MockLittleFS {
 public:
  File open(const char*, const char* = "r") { return File(); }
  bool exists(const char*) { return false; }
};

extern MockLittleFS LittleFS;
