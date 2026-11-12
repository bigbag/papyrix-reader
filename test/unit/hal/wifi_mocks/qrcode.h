#pragma once

#include <cstdint>
#include <cstdlib>

struct QRCode {
  uint8_t size;
};
constexpr uint8_t ECC_LOW = 0;
[[noreturn]] inline uint16_t qrcode_getBufferSize(uint8_t) { std::abort(); }
[[noreturn]] inline int8_t qrcode_initText(QRCode*, uint8_t*, uint8_t, uint8_t, const char*) { std::abort(); }
[[noreturn]] inline bool qrcode_getModule(QRCode*, uint8_t, uint8_t) { std::abort(); }
