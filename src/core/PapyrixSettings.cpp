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
#include "SettingsSerialization.h"

#define TAG "SETTINGS"

namespace papyrix {

namespace {
static constexpr char kSettingsTmp[] = PAPYRIX_SETTINGS_FILE ".tmp";

const char* settingsReadStatusName(const SettingsReadStatus status) {
  switch (status) {
    case SettingsReadStatus::InvalidMagic:
      return "invalid magic";
    case SettingsReadStatus::UnsupportedVersion:
      return "unsupported version";
    case SettingsReadStatus::Truncated:
      return "truncated data";
    default:
      return "ok";
  }
}

// Reject smaller tmp writes so a truncated file never renames over a good
// settings.bin. Each term mirrors the matching writePod()/write() call in
// save()/saveToFile(); referencing the actual Settings members (via a constexpr
// default instance) makes a field removal or type change a compile error here,
// so this can't silently drift and falsely reject valid saves.
constexpr Settings kSizeProbe{};
constexpr uint32_t kMinSettingsBytes =
    sizeof(uint32_t) +  // kSettingsMagic
    sizeof(uint8_t) +   // kSettingsFileVersion
    sizeof(uint8_t) +   // kSettingsFieldCount
    sizeof(kSizeProbe.sleepScreen) + sizeof(kSizeProbe.textLayout) + sizeof(kSizeProbe.shortPwrBtn) +
    sizeof(kSizeProbe.statusBar) + sizeof(kSizeProbe.orientation) + sizeof(kSizeProbe.fontSize) +
    sizeof(kSizeProbe.pagesPerRefresh) + sizeof(kSizeProbe.sideButtonLayout) + sizeof(kSizeProbe.autoSleepMinutes) +
    sizeof(kSizeProbe.paragraphAlignment) + sizeof(kSizeProbe.hyphenation) + sizeof(kSizeProbe.textAntiAliasing) +
    sizeof(kSizeProbe.showImages) + sizeof(kSizeProbe.startupBehavior) + sizeof(kSizeProbe._reserved) +
    sizeof(kSizeProbe.lineSpacing) + sizeof(kSizeProbe.themeName) + sizeof(kSizeProbe.lastBookPath) +
    sizeof(kSizeProbe.pendingTransition) + sizeof(kSizeProbe.transitionReturnTo) +
    sizeof(kSizeProbe.sunlightFadingFix) + sizeof(kSizeProbe.fileListDir) + sizeof(kSizeProbe.fileListSelectedName) +
    sizeof(kSizeProbe.fileListSelectedIndex) + sizeof(kSizeProbe.frontButtonLayout) +
    sizeof(kSizeProbe.fullBookProcess) + sizeof(kSizeProbe.showRecents) + sizeof(kSizeProbe.recycleBinEnabled);
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

  const bool writeOk = writeSettingsFile(outputFile, *this);
  const uint32_t writtenBytes = outputFile.size();
  outputFile.close();

  if (!writeOk || writtenBytes < kMinSettingsBytes) {
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
  if (!result.ok()) return result;

  Settings decoded;
  const SettingsReadStatus status = readSettingsFile(inputFile, *this, decoded);
  inputFile.close();

  if (status == SettingsReadStatus::Ok) {
    *this = decoded;
    LOG_INF(TAG, "Settings loaded from file");
    return Ok();
  }

  LOG_ERR(TAG, "Failed to load settings: %s", settingsReadStatusName(status));
  if (status == SettingsReadStatus::InvalidMagic || status == SettingsReadStatus::Truncated) {
    storage.remove(PAPYRIX_SETTINGS_FILE);
  }
  if (status == SettingsReadStatus::UnsupportedVersion || status == SettingsReadStatus::InvalidMagic) {
    return ErrVoid(Error::UnsupportedVersion);
  }
  return ErrVoid(Error::IOError);
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
  const int fontId = getReaderFontId(theme);
  return RenderConfig(fontId, getLineCompression(), getIndentLevel(), getSpacingLevel(), paragraphAlignment,
                      static_cast<bool>(hyphenation), static_cast<bool>(showImages), viewportWidth, viewportHeight, 0,
                      FONT_MANAGER.activeReaderFontFingerprint());
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

  const bool writeOk = writeSettingsFile(outputFile, *this);
  const uint32_t writtenBytes = outputFile.size();
  outputFile.close();

  if (!writeOk || writtenBytes < kMinSettingsBytes) {
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
  if (!SdMan.openFileForRead("SET", PAPYRIX_SETTINGS_FILE, inputFile)) return false;

  Settings decoded;
  const SettingsReadStatus status = readSettingsFile(inputFile, *this, decoded);
  inputFile.close();

  if (status == SettingsReadStatus::Ok) {
    *this = decoded;
    LOG_INF(TAG, "Settings loaded from file");
    return true;
  }

  LOG_ERR(TAG, "Failed to load settings: %s", settingsReadStatusName(status));
  if (status == SettingsReadStatus::InvalidMagic || status == SettingsReadStatus::Truncated) {
    SdMan.remove(PAPYRIX_SETTINGS_FILE);
  }
  return false;
}

}  // namespace papyrix
