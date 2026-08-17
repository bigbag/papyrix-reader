#pragma once

#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;

  virtual size_t write(const uint8_t* buf, size_t size) {
    (void)buf;
    return size;
  }

  virtual size_t write(uint8_t value) {
    (void)value;
    return 1;
  }
};
