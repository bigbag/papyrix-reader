#pragma once

#include <cstdint>

inline uint8_t quantize(int, int, int) { return 0; }
inline uint8_t quantizeSimple(int) { return 0; }
inline uint8_t quantize1bit(int, int, int) { return 0; }
inline int adjustPixel(int gray) { return gray; }
inline uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
}
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int) {}
  ~AtkinsonDitherer() = default;
  uint8_t processPixel(int, int) { return 0; }
  void nextRow() {}
  void reset() {}
};

class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int) {}
  ~FloydSteinbergDitherer() = default;
  uint8_t processPixel(int, int) { return 0; }
  void nextRow() {}
  bool isReverseRow() const { return false; }
  void reset() {}
};

class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int) {}
  ~Atkinson1BitDitherer() = default;
  uint8_t processPixel(int, int) { return 0; }
  void nextRow() {}
  void reset() {}
};
