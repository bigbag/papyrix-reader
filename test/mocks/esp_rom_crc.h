#pragma once

#include <cstddef>
#include <cstdint>

inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
  }
  return crc;
}
