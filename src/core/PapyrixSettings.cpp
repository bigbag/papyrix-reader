#include "PapyrixSettings.h"

#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <Serialization.h>

#include "../FontManager.h"
#include "../Theme.h"
#include "../config.h"
#include "../drivers/Device.h"
#include "../drivers/Storage.h"

#define TAG "SETTINGS"

namespace papyrix {

namespace {
// Magic signature to identify Papyrix settings files ("PPXS" in little-endian)
constexpr uint32_t SETTINGS_MAGIC = 0x53585050;
// Minimum version we can read (allows backward compatibility)
constexpr uint8_t MIN_SETTINGS_VERSION = 3;
// Version 11: Increased path buffer sizes (lastBookPath 256→512, fileListDir 256→512, fileListSelectedName 128→256)
constexpr uint8_t SETTINGS_FILE_VERSION = 12;
// Increment this when adding new persisted settings fields
constexpr uint8_t SETTINGS_COUNT = 27;

static constexpr char kSettingsTmp[] = PAPYRIX_SETTINGS_FILE ".tmp";

// Reject smaller tmp writes so a truncated file never renames over a good
// settings.bin. Each term mirrors the matching writePod()/write() call in
// save()/saveToFile(); referencing the actual Settings members (via a constexpr
// default instance) makes a field removal or type change a compile error here,
// so this can't silently drift and falsely reject valid saves.
constexpr Settings kSizeProbe{};
constexpr uint32_t kMinSettingsBytes =
    sizeof(uint32_t) +  // SETTINGS_MAGIC
    sizeof(uint8_t) +   // SETTINGS_FILE_VERSION
    sizeof(uint8_t) +   // SETTINGS_COUNT
    sizeof(kSizeProbe.sleepScreen) + sizeof(kSizeProbe.textLayout) + sizeof(kSizeProbe.shortPwrBtn) +
    sizeof(kSizeProbe.statusBar) + sizeof(kSizeProbe.orientation) + sizeof(kSizeProbe.fontSize) +
    sizeof(kSizeProbe.pagesPerRefresh) + sizeof(kSizeProbe.sideButtonLayout) + sizeof(kSizeProbe.autoSleepMinutes) +
    sizeof(kSizeProbe.paragraphAlignment) + sizeof(kSizeProbe.hyphenation) + sizeof(kSizeProbe.textAntiAliasing) +
    sizeof(kSizeProbe.showImages) + sizeof(kSizeProbe.startupBehavior) + sizeof(kSizeProbe._reserved) +
    sizeof(kSizeProbe.lineSpacing) + sizeof(kSizeProbe.themeName) + sizeof(kSizeProbe.lastBookPath) +
    sizeof(kSizeProbe.pendingTransition) + sizeof(kSizeProbe.transitionReturnTo) +
    sizeof(kSizeProbe.sunlightFadingFix) + sizeof(kSizeProbe.fileListDir) + sizeof(kSizeProbe.fileListSelectedName) +
    sizeof(kSizeProbe.fileListSelectedIndex) + sizeof(kSizeProbe.frontButtonLayout) +
    sizeof(kSizeProbe.fullBookProcess) + sizeof(kSizeProbe.showRecents);
}  // namespace

Result<void> Settings::save(drivers::Storage& storage) const {
  // Make sure the directories exist
  storage.mkdir(PAPYRIX_DIR);
  storage.mkdir(PAPYRIX_CACHE_DIR);
  // X3 caches live in a subdirectory so an SD card moved between an X4 and an X3
  // doesn't load X4-shaped page layouts on the X3 panel. No-op on X4 (path matches above).
  storage.mkdir(drivers::Device::instance().cacheDir());

  // Publish via temp so readers never see a partial write. SdFat can't rename
  // over an existing file, so commitFile removes the stale final first.
  FsFile outputFile;
  auto result = storage.openWrite(kSettingsTmp, outputFile);
  if (!result.ok()) {
    return result;
  }

  serialization::writePod(outputFile, SETTINGS_MAGIC);
  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, textLayout);
  serialization::writePod(outputFile, shortPwrBtn);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, pagesPerRefresh);
  serialization::writePod(outputFile, sideButtonLayout);
  serialization::writePod(outputFile, autoSleepMinutes);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, hyphenation);
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writePod(outputFile, showImages);
  serialization::writePod(outputFile, startupBehavior);
  serialization::writePod(outputFile, _reserved);
  serialization::writePod(outputFile, lineSpacing);
  // Write themeName as fixed-length string
  outputFile.write(reinterpret_cast<const uint8_t*>(themeName), sizeof(themeName));
  outputFile.write(reinterpret_cast<const uint8_t*>(lastBookPath), sizeof(lastBookPath));
  serialization::writePod(outputFile, pendingTransition);
  serialization::writePod(outputFile, transitionReturnTo);
  serialization::writePod(outputFile, sunlightFadingFix);
  outputFile.write(reinterpret_cast<const uint8_t*>(fileListDir), sizeof(fileListDir));
  outputFile.write(reinterpret_cast<const uint8_t*>(fileListSelectedName), sizeof(fileListSelectedName));
  serialization::writePod(outputFile, fileListSelectedIndex);
  serialization::writePod(outputFile, frontButtonLayout);
  serialization::writePod(outputFile, fullBookProcess);
  serialization::writePod(outputFile, showRecents);
  outputFile.sync();
  const uint32_t writtenBytes = outputFile.size();
  outputFile.close();

  if (writtenBytes < kMinSettingsBytes) {
    LOG_ERR(TAG, "Short settings write %u/%u; discarding tmp", static_cast<unsigned>(writtenBytes),
            static_cast<unsigned>(kMinSettingsBytes));
    storage.remove(kSettingsTmp);
    return ErrVoid(Error::IOError);
  }
  if (!storage.commitFile(kSettingsTmp, PAPYRIX_SETTINGS_FILE).ok()) {
    LOG_ERR(TAG, "Failed to commit settings file");
    storage.remove(kSettingsTmp);
    return ErrVoid(Error::IOError);
  }

  LOG_INF(TAG, "Settings saved to file");
  return Ok();
}

Result<void> Settings::load(drivers::Storage& storage) {
  FsFile inputFile;
  auto result = storage.openRead(PAPYRIX_SETTINGS_FILE, inputFile);
  if (!result.ok()) {
    return result;
  }

  // Check magic signature to detect incompatible settings files (e.g., from Crosspoint firmware)
  uint32_t magic;
  serialization::readPod(inputFile, magic);
  if (magic != SETTINGS_MAGIC) {
    LOG_ERR(TAG, "Invalid settings file (wrong magic 0x%08X), deleting", magic);
    inputFile.close();
    storage.remove(PAPYRIX_SETTINGS_FILE);
    return ErrVoid(Error::UnsupportedVersion);
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version < MIN_SETTINGS_VERSION || version > SETTINGS_FILE_VERSION) {
    LOG_ERR(TAG, "Deserialization failed: Unknown version %u", version);
    inputFile.close();
    return ErrVoid(Error::UnsupportedVersion);
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  // Cap fileSettingsCount to prevent reading garbage from corrupted files
  if (fileSettingsCount > SETTINGS_COUNT) {
    LOG_ERR(TAG, "fileSettingsCount %u exceeds max %u, capping", fileSettingsCount, SETTINGS_COUNT);
    fileSettingsCount = SETTINGS_COUNT;
  }

  // Load settings that exist (support older files with fewer fields)
  // readPodValidated keeps default value if read value >= maxValue
  uint8_t settingsRead = 0;
  do {
    serialization::readPodValidated(inputFile, sleepScreen, uint8_t(5));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, textLayout, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, shortPwrBtn, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, statusBar, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, orientation, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, fontSize, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pagesPerRefresh, uint8_t(6));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, sideButtonLayout, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, autoSleepMinutes, uint8_t(5));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, paragraphAlignment, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, hyphenation, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, textAntiAliasing, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, showImages, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, startupBehavior, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, _reserved, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, lineSpacing, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    // Read themeName as fixed-length string
    inputFile.read(reinterpret_cast<uint8_t*>(themeName), sizeof(themeName));
    themeName[sizeof(themeName) - 1] = '\0';  // Ensure null-terminated
    if (++settingsRead >= fileSettingsCount) break;
    if (version <= 10) {
      inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), 256);
      memset(lastBookPath + 256, 0, sizeof(lastBookPath) - 256);
    } else {
      inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), sizeof(lastBookPath));
    }
    lastBookPath[sizeof(lastBookPath) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pendingTransition, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, transitionReturnTo, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, sunlightFadingFix, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    if (version <= 10) {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListDir), 256);
      memset(fileListDir + 256, 0, sizeof(fileListDir) - 256);
    } else {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListDir), sizeof(fileListDir));
    }
    fileListDir[sizeof(fileListDir) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    if (version <= 10) {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListSelectedName), 128);
      memset(fileListSelectedName + 128, 0, sizeof(fileListSelectedName) - 128);
    } else {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListSelectedName), sizeof(fileListSelectedName));
    }
    fileListSelectedName[sizeof(fileListSelectedName) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, fileListSelectedIndex);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, frontButtonLayout, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, fullBookProcess, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, showRecents, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  if (version < 8) {
    fontSize++;
  }

  inputFile.close();
  LOG_INF(TAG, "Settings loaded from file");
  return Ok();
}

int Settings::getReaderFontId(const Theme& theme) const {
  switch (fontSize) {
    case FontXSmall:
      return FONT_MANAGER.getReaderFontId(theme.readerFontFamilyXSmall, theme.readerFontIdXSmall);
    case FontMedium:
      return FONT_MANAGER.getReaderFontId(theme.readerFontFamilyMedium, theme.readerFontIdMedium);
    case FontLarge:
      return FONT_MANAGER.getReaderFontId(theme.readerFontFamilyLarge, theme.readerFontIdLarge);
    default:  // FontSmall
      return FONT_MANAGER.getReaderFontId(theme.readerFontFamilySmall, theme.readerFontId);
  }
}

bool Settings::hasExternalReaderFont(const Theme& theme) const {
  const char* family = nullptr;
  switch (fontSize) {
    case FontXSmall:
      family = theme.readerFontFamilyXSmall;
      break;
    case FontMedium:
      family = theme.readerFontFamilyMedium;
      break;
    case FontLarge:
      family = theme.readerFontFamilyLarge;
      break;
    default:
      family = theme.readerFontFamilySmall;
      break;
  }
  return family && *family;
}

RenderConfig Settings::getRenderConfig(const Theme& theme, uint16_t viewportWidth, uint16_t viewportHeight) const {
  return RenderConfig(getReaderFontId(theme), getLineCompression(), getIndentLevel(), getSpacingLevel(),
                      paragraphAlignment, static_cast<bool>(hyphenation), static_cast<bool>(showImages), viewportWidth,
                      viewportHeight);
}

// Legacy methods that use SdMan directly (for early init before Core)
bool Settings::saveToFile() const {
  SdMan.mkdir(PAPYRIX_DIR);
  SdMan.mkdir(PAPYRIX_CACHE_DIR);
  SdMan.mkdir(drivers::Device::instance().cacheDir());

  FsFile outputFile;
  if (!SdMan.openFileForWrite("SET", kSettingsTmp, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, SETTINGS_MAGIC);
  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, textLayout);
  serialization::writePod(outputFile, shortPwrBtn);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, pagesPerRefresh);
  serialization::writePod(outputFile, sideButtonLayout);
  serialization::writePod(outputFile, autoSleepMinutes);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, hyphenation);
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writePod(outputFile, showImages);
  serialization::writePod(outputFile, startupBehavior);
  serialization::writePod(outputFile, _reserved);
  serialization::writePod(outputFile, lineSpacing);
  outputFile.write(reinterpret_cast<const uint8_t*>(themeName), sizeof(themeName));
  outputFile.write(reinterpret_cast<const uint8_t*>(lastBookPath), sizeof(lastBookPath));
  serialization::writePod(outputFile, pendingTransition);
  serialization::writePod(outputFile, transitionReturnTo);
  serialization::writePod(outputFile, sunlightFadingFix);
  outputFile.write(reinterpret_cast<const uint8_t*>(fileListDir), sizeof(fileListDir));
  outputFile.write(reinterpret_cast<const uint8_t*>(fileListSelectedName), sizeof(fileListSelectedName));
  serialization::writePod(outputFile, fileListSelectedIndex);
  serialization::writePod(outputFile, frontButtonLayout);
  serialization::writePod(outputFile, fullBookProcess);
  serialization::writePod(outputFile, showRecents);
  outputFile.sync();
  const uint32_t writtenBytes = outputFile.size();
  outputFile.close();

  if (writtenBytes < kMinSettingsBytes) {
    LOG_ERR(TAG, "Short settings write %u/%u; discarding tmp", static_cast<unsigned>(writtenBytes),
            static_cast<unsigned>(kMinSettingsBytes));
    SdMan.remove(kSettingsTmp);
    return false;
  }
  if (!SdMan.commitFile(kSettingsTmp, PAPYRIX_SETTINGS_FILE)) {
    LOG_ERR(TAG, "Failed to commit settings file");
    SdMan.remove(kSettingsTmp);
    return false;
  }

  LOG_INF(TAG, "Settings saved to file");
  return true;
}

bool Settings::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("SET", PAPYRIX_SETTINGS_FILE, inputFile)) {
    return false;
  }

  // Check magic signature to detect incompatible settings files (e.g., from Crosspoint firmware)
  uint32_t magic;
  serialization::readPod(inputFile, magic);
  if (magic != SETTINGS_MAGIC) {
    LOG_ERR(TAG, "Invalid settings file (wrong magic 0x%08X), deleting", magic);
    inputFile.close();
    SdMan.remove(PAPYRIX_SETTINGS_FILE);
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version < MIN_SETTINGS_VERSION || version > SETTINGS_FILE_VERSION) {
    LOG_ERR(TAG, "Deserialization failed: Unknown version %u", version);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  // Cap fileSettingsCount to prevent reading garbage from corrupted files
  if (fileSettingsCount > SETTINGS_COUNT) {
    LOG_ERR(TAG, "fileSettingsCount %u exceeds max %u, capping", fileSettingsCount, SETTINGS_COUNT);
    fileSettingsCount = SETTINGS_COUNT;
  }

  uint8_t settingsRead = 0;
  do {
    serialization::readPodValidated(inputFile, sleepScreen, uint8_t(5));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, textLayout, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, shortPwrBtn, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, statusBar, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, orientation, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, fontSize, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pagesPerRefresh, uint8_t(6));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, sideButtonLayout, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, autoSleepMinutes, uint8_t(5));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, paragraphAlignment, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, hyphenation, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, textAntiAliasing, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, showImages, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, startupBehavior, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, _reserved, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, lineSpacing, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    inputFile.read(reinterpret_cast<uint8_t*>(themeName), sizeof(themeName));
    themeName[sizeof(themeName) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    if (version <= 10) {
      inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), 256);
      memset(lastBookPath + 256, 0, sizeof(lastBookPath) - 256);
    } else {
      inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), sizeof(lastBookPath));
    }
    lastBookPath[sizeof(lastBookPath) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pendingTransition, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, transitionReturnTo, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, sunlightFadingFix, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    if (version <= 10) {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListDir), 256);
      memset(fileListDir + 256, 0, sizeof(fileListDir) - 256);
    } else {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListDir), sizeof(fileListDir));
    }
    fileListDir[sizeof(fileListDir) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    if (version <= 10) {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListSelectedName), 128);
      memset(fileListSelectedName + 128, 0, sizeof(fileListSelectedName) - 128);
    } else {
      inputFile.read(reinterpret_cast<uint8_t*>(fileListSelectedName), sizeof(fileListSelectedName));
    }
    fileListSelectedName[sizeof(fileListSelectedName) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, fileListSelectedIndex);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, frontButtonLayout, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, fullBookProcess, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, showRecents, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  if (version < 8) {
    fontSize++;
  }

  inputFile.close();
  LOG_INF(TAG, "Settings loaded from file");
  return true;
}

}  // namespace papyrix
