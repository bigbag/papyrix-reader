#include "SettingsSerialization.h"

#include <Serialization.h>

#include <cstring>

#include "PapyrixSettings.h"

namespace papyrix {
namespace {

bool readBytes(FsFile& file, void* destination, const size_t size) {
  return file.read(static_cast<uint8_t*>(destination), size) == static_cast<int>(size);
}

bool writeBytes(FsFile& file, const void* source, const size_t size) {
  return file.write(static_cast<const uint8_t*>(source), size) == size;
}

}  // namespace

bool writeSettingsFile(FsFile& file, const Settings& settings) {
  return serialization::writePodChecked(file, kSettingsMagic) &&
         serialization::writePodChecked(file, kSettingsFileVersion) &&
         serialization::writePodChecked(file, kSettingsFieldCount) &&
         serialization::writePodChecked(file, settings.sleepScreen) &&
         serialization::writePodChecked(file, settings.textLayout) &&
         serialization::writePodChecked(file, settings.shortPwrBtn) &&
         serialization::writePodChecked(file, settings.statusBar) &&
         serialization::writePodChecked(file, settings.orientation) &&
         serialization::writePodChecked(file, settings.fontSize) &&
         serialization::writePodChecked(file, settings.pagesPerRefresh) &&
         serialization::writePodChecked(file, settings.sideButtonLayout) &&
         serialization::writePodChecked(file, settings.autoSleepMinutes) &&
         serialization::writePodChecked(file, settings.paragraphAlignment) &&
         serialization::writePodChecked(file, settings.hyphenation) &&
         serialization::writePodChecked(file, settings.textAntiAliasing) &&
         serialization::writePodChecked(file, settings.showImages) &&
         serialization::writePodChecked(file, settings.startupBehavior) &&
         serialization::writePodChecked(file, settings._reserved) &&
         serialization::writePodChecked(file, settings.lineSpacing) &&
         writeBytes(file, settings.themeName, sizeof(settings.themeName)) &&
         writeBytes(file, settings.lastBookPath, sizeof(settings.lastBookPath)) &&
         serialization::writePodChecked(file, settings.pendingTransition) &&
         serialization::writePodChecked(file, settings.transitionReturnTo) &&
         serialization::writePodChecked(file, settings.sunlightFadingFix) &&
         writeBytes(file, settings.fileListDir, sizeof(settings.fileListDir)) &&
         writeBytes(file, settings.fileListSelectedName, sizeof(settings.fileListSelectedName)) &&
         serialization::writePodChecked(file, settings.fileListSelectedIndex) &&
         serialization::writePodChecked(file, settings.frontButtonLayout) &&
         serialization::writePodChecked(file, settings.fullBookProcess) &&
         serialization::writePodChecked(file, settings.showRecents) && file.sync();
}

SettingsReadStatus readSettingsFile(FsFile& file, const Settings& defaults, Settings& decoded) {
  Settings candidate = defaults;
  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t fieldCount = 0;

  if (!serialization::readPodChecked(file, magic) || !serialization::readPodChecked(file, version) ||
      !serialization::readPodChecked(file, fieldCount)) {
    return SettingsReadStatus::Truncated;
  }
  if (magic != kSettingsMagic) return SettingsReadStatus::InvalidMagic;
  if (version < kMinSettingsVersion || version > kSettingsFileVersion) {
    return SettingsReadStatus::UnsupportedVersion;
  }
  if (fieldCount > kSettingsFieldCount) return SettingsReadStatus::Truncated;

  uint8_t fieldsRead = 0;
#define READ_SETTING(expression)                             \
  do {                                                       \
    if (fieldsRead >= fieldCount) goto complete;             \
    fieldsRead++;                                            \
    if (!(expression)) return SettingsReadStatus::Truncated; \
  } while (false)

  READ_SETTING(serialization::readPodValidated(file, candidate.sleepScreen, uint8_t(5)));
  READ_SETTING(serialization::readPodValidated(file, candidate.textLayout, uint8_t(3)));
  READ_SETTING(serialization::readPodValidated(file, candidate.shortPwrBtn, uint8_t(4)));
  READ_SETTING(serialization::readPodValidated(file, candidate.statusBar, uint8_t(3)));
  READ_SETTING(serialization::readPodValidated(file, candidate.orientation, uint8_t(4)));
  READ_SETTING(serialization::readPodValidated(file, candidate.fontSize, uint8_t(4)));
  READ_SETTING(serialization::readPodValidated(file, candidate.pagesPerRefresh, uint8_t(6)));
  READ_SETTING(serialization::readPodValidated(file, candidate.sideButtonLayout, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate.autoSleepMinutes, uint8_t(5)));
  READ_SETTING(serialization::readPodValidated(file, candidate.paragraphAlignment, uint8_t(4)));
  READ_SETTING(serialization::readPodValidated(file, candidate.hyphenation, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate.textAntiAliasing, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate.showImages, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate.startupBehavior, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate._reserved, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate.lineSpacing, uint8_t(4)));
  READ_SETTING(readBytes(file, candidate.themeName, sizeof(candidate.themeName)));
  candidate.themeName[sizeof(candidate.themeName) - 1] = '\0';

  if (version <= 10) {
    READ_SETTING(readBytes(file, candidate.lastBookPath, 256));
    memset(candidate.lastBookPath + 256, 0, sizeof(candidate.lastBookPath) - 256);
  } else if (version <= 12) {
    READ_SETTING(readBytes(file, candidate.lastBookPath, 512));
    memset(candidate.lastBookPath + 512, 0, sizeof(candidate.lastBookPath) - 512);
  } else {
    READ_SETTING(readBytes(file, candidate.lastBookPath, sizeof(candidate.lastBookPath)));
  }
  candidate.lastBookPath[sizeof(candidate.lastBookPath) - 1] = '\0';

  READ_SETTING(serialization::readPodValidated(file, candidate.pendingTransition, uint8_t(3)));
  READ_SETTING(serialization::readPodValidated(file, candidate.transitionReturnTo, uint8_t(3)));
  READ_SETTING(serialization::readPodValidated(file, candidate.sunlightFadingFix, uint8_t(2)));

  if (version <= 10) {
    READ_SETTING(readBytes(file, candidate.fileListDir, 256));
    memset(candidate.fileListDir + 256, 0, sizeof(candidate.fileListDir) - 256);
  } else if (version <= 12) {
    READ_SETTING(readBytes(file, candidate.fileListDir, 512));
    memset(candidate.fileListDir + 512, 0, sizeof(candidate.fileListDir) - 512);
  } else {
    READ_SETTING(readBytes(file, candidate.fileListDir, sizeof(candidate.fileListDir)));
  }
  candidate.fileListDir[sizeof(candidate.fileListDir) - 1] = '\0';

  if (version <= 10) {
    READ_SETTING(readBytes(file, candidate.fileListSelectedName, 128));
    memset(candidate.fileListSelectedName + 128, 0, sizeof(candidate.fileListSelectedName) - 128);
  } else {
    READ_SETTING(readBytes(file, candidate.fileListSelectedName, sizeof(candidate.fileListSelectedName)));
  }
  candidate.fileListSelectedName[sizeof(candidate.fileListSelectedName) - 1] = '\0';

  READ_SETTING(serialization::readPodChecked(file, candidate.fileListSelectedIndex));
  READ_SETTING(serialization::readPodValidated(file, candidate.frontButtonLayout, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate.fullBookProcess, uint8_t(2)));
  READ_SETTING(serialization::readPodValidated(file, candidate.showRecents, uint8_t(2)));

complete:
#undef READ_SETTING
  if (version < 8) candidate.fontSize++;
  decoded = candidate;
  return SettingsReadStatus::Ok;
}

}  // namespace papyrix
