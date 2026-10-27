#pragma once

#include <cstddef>
#include <cstdint>

namespace papyrix::firmware_board {

extern const char kTag[];

class Scanner {
 public:
  void feed(const uint8_t* data, size_t length);
  void endSegment();
  bool compatible() const { return !invalid_ && !capturing_; }

 private:
  uint8_t matched_ = 0;
  uint8_t nameLength_ = 0;
  bool capturing_ = false;
  bool found_ = false;
  bool invalid_ = false;
};

}  // namespace papyrix::firmware_board
