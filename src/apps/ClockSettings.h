#pragma once

#include <cstddef>
#include <cstdint>

#include "ClockFaces.h"

namespace papyrix::hal {
class Storage;
}

namespace papyrix::clock_app {

enum class PowerMode : uint8_t { Normal = 0, PowerSaving = 1 };

struct ClockSettings {
  clock_faces::Face face = clock_faces::Face::Big;
  int8_t utcOffset = 0;
  bool use24h = true;
  int8_t dateFormat = 0;
  int8_t ntpSyncSetting = 0;
  char ntpServers[128] = "pool.ntp.org,time.nist.gov";
  PowerMode powerMode = PowerMode::Normal;
};

inline constexpr uint32_t NTP_SYNC_SECONDS[] = {10800, 21600, 86400, 0};

inline bool validSettings(const ClockSettings& value) {
  if (!clock_faces::isSelectable(value.face) || value.utcOffset < -12 || value.utcOffset > 14 ||
      *reinterpret_cast<const uint8_t*>(&value.use24h) > 1 || value.dateFormat < 0 || value.dateFormat > 3 ||
      value.ntpSyncSetting < 0 || value.ntpSyncSetting > 3 || value.powerMode > PowerMode::PowerSaving) {
    return false;
  }
  for (char byte : value.ntpServers) {
    if (byte == '\0') return true;
    if (static_cast<uint8_t>(byte) < 0x20 || static_cast<uint8_t>(byte) > 0x7E) return false;
  }
  return false;
}
ClockSettings decodeSettings(const char* text, size_t length, bool fileExists);
size_t encodeSettings(const ClockSettings& value, char* out, size_t capacity);
bool loadSettings(hal::Storage& storage, ClockSettings& out);
bool saveSettings(hal::Storage& storage, const ClockSettings& value);

}  // namespace papyrix::clock_app
