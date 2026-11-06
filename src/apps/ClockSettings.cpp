#include "ClockSettings.h"

#include <CheckedFile.h>
#include <SdFat.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../hal/Storage.h"

namespace papyrix::clock_app {
namespace {
constexpr const char* kPath = "/.papyrix/apps/clock.txt";
constexpr const char* kTemporaryPath = "/.papyrix/apps/clock.txt.tmp";
constexpr const char* kBackupPath = "/.papyrix/apps/clock.txt.bak";
constexpr size_t kBufferSize = 320;
}  // namespace

ClockSettings decodeSettings(const char* text, size_t length, bool fileExists) {
  ClockSettings value;
  if (!fileExists) return value;
  value.face = clock_faces::Face::Retro;
  if (!text) return value;
  std::string_view remaining(text, length);
  while (!remaining.empty()) {
    const auto end = remaining.find('\n');
    auto line = remaining.substr(0, end);
    remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto separator = line.find('=');
    if (separator == std::string_view::npos) continue;
    const auto key = line.substr(0, separator);
    const auto input = line.substr(separator + 1);
    if (key == "ntpServers") {
      const size_t count = std::min(input.size(), sizeof(value.ntpServers) - 1);
      std::memcpy(value.ntpServers, input.data(), count);
      value.ntpServers[count] = '\0';
      continue;
    }
    int number = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size()) continue;
    if (key == "utcOffset")
      value.utcOffset = static_cast<int8_t>(std::clamp(number, -12, 14));
    else if (key == "use24h" && (number == 0 || number == 1))
      value.use24h = number == 1;
    else if (key == "ntpSync" && number >= 0 && number < 4)
      value.ntpSyncSetting = static_cast<int8_t>(number);
    else if (key == "dateFmt" && number >= 0 && number < 4)
      value.dateFormat = static_cast<int8_t>(number);
    else if (key == "powerMode" && (number == 0 || number == 1))
      value.powerMode = static_cast<PowerMode>(number);
    else if (key == "face" && number >= 0 && number < static_cast<int>(clock_faces::Face::Count) &&
             clock_faces::isSelectable(static_cast<clock_faces::Face>(number))) {
      value.face = static_cast<clock_faces::Face>(number);
    }
  }
  return value;
}

size_t encodeSettings(const ClockSettings& value, char* out, size_t capacity) {
  if (!out || capacity == 0 || !validSettings(value)) return 0;
  const int length = std::snprintf(
      out, capacity, "utcOffset=%d\nuse24h=%d\nntpSync=%d\ndateFmt=%d\nntpServers=%s\nface=%u\npowerMode=%u\n",
      value.utcOffset, value.use24h ? 1 : 0, value.ntpSyncSetting, value.dateFormat, value.ntpServers,
      static_cast<unsigned>(value.face), static_cast<unsigned>(value.powerMode));
  return length > 0 && static_cast<size_t>(length) < capacity ? static_cast<size_t>(length) : 0;
}

bool loadSettings(hal::Storage& storage, ClockSettings& out) {
  const auto final = storage.exists(kPath);
  if (!final.ok()) return false;
  const char* path = kPath;
  if (!final.value) {
    const auto backup = storage.exists(kBackupPath);
    if (!backup.ok()) return false;
    if (!backup.value) {
      out = ClockSettings{};
      return true;
    }
    path = kBackupPath;
  }
  FsFile file;
  if (!storage.openRead(path, file).ok()) return false;
  char buffer[kBufferSize];
  const auto length = file.fileSize();
  if (length >= sizeof(buffer)) {
    file.close();
    return false;
  }
  const int read = file.read(reinterpret_cast<uint8_t*>(buffer), length);
  const bool closed = file.close();
  if (read < 0 || static_cast<size_t>(read) != length || !closed) return false;
  const auto settings = decodeSettings(buffer, length, true);
  if (!validSettings(settings)) return false;
  out = settings;
  return true;
}

bool saveSettings(hal::Storage& storage, const ClockSettings& value) {
  char buffer[kBufferSize];
  const size_t length = encodeSettings(value, buffer, sizeof(buffer));
  if (!length) return false;
  const auto directory = storage.exists("/.papyrix/apps");
  if (!directory.ok() || (!directory.value && !storage.mkdir("/.papyrix/apps").ok())) return false;
  auto final = storage.exists(kPath);
  const auto backup = storage.exists(kBackupPath);
  if (!final.ok() || !backup.ok()) return false;
  if (!final.value && backup.value) {
    if (!storage.rename(kBackupPath, kPath).ok()) return false;
    final.value = true;
  } else if (final.value && backup.value && !storage.remove(kBackupPath).ok()) {
    return false;
  }
  return miniapps::publishCheckedFile<FsFile>(
      storage, kPath, kTemporaryPath, kBackupPath, final.value,
      [&](FsFile& file) { return file.write(reinterpret_cast<const uint8_t*>(buffer), length) == length; });
}

}  // namespace papyrix::clock_app
