#pragma once

#include <SdFat.h>

#include <cstdint>

namespace papyrix {

struct Settings;

inline constexpr uint32_t kSettingsMagic = 0x53585050;
inline constexpr uint8_t kMinSettingsVersion = 3;
inline constexpr uint8_t kSettingsFileVersion = 13;
inline constexpr uint8_t kSettingsFieldCount = 27;

enum class SettingsReadStatus { Ok, InvalidMagic, UnsupportedVersion, Truncated };

bool writeSettingsFile(FsFile& file, const Settings& settings);
SettingsReadStatus readSettingsFile(FsFile& file, const Settings& defaults, Settings& decoded);

}  // namespace papyrix
