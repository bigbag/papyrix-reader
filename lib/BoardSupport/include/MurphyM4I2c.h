#pragma once

#include <cstddef>
#include <cstdint>

namespace papyrix::board::murphy_m4_i2c {
enum class Device : uint8_t { Touch, Rtc };
bool begin();
bool read(Device device, uint8_t reg, uint8_t* data, size_t length);
bool write(Device device, const uint8_t* data, size_t length);
}  // namespace papyrix::board::murphy_m4_i2c
