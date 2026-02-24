#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

unsigned long millis();

class Print {
 public:
  virtual size_t write(const uint8_t* buf, size_t size) {
    (void)buf;
    return size;
  }
  virtual size_t write(uint8_t c) {
    (void)c;
    return 1;
  }
  virtual ~Print() = default;
  virtual void flush() {}
};

struct CaptureState {
  std::string output;
  bool enabled = true;
};

extern CaptureState captureState;

class HWCDC : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return captureState.enabled; }
  void print(const char* s) {
    if (s) captureState.output += s;
  }
  size_t write(uint8_t c) override {
    captureState.output += static_cast<char>(c);
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    captureState.output.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
  void flush() override {}
};

extern HWCDC Serial;
