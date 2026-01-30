#include "PapyrixSettings.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <Serialization.h>

#include "../FontManager.h"
#include "../Theme.h"
#include "../config.h"
#include "../drivers/Storage.h"

namespace papyrix {

namespace {
// Minimum version we can read (allows backward compatibility)
constexpr uint8_t MIN_SETTINGS_VERSION = 3;
// Version 4: Added sunlightFadingFix
constexpr uint8_t SETTINGS_FILE_VERSION = 4;
// Increment this when adding new persisted settings fields
constexpr uint8_t SETTINGS_COUNT = 19;
}  // namespace

Result<void> Settings::save(drivers::Storage& storage) const {
  // Make sure the directories exist
  storage.mkdir(PAPYRIX_DIR);
  storage.mkdir(PAPYRIX_CACHE_DIR);

  FsFile outputFile;
  auto result = storage.openWrite(PAPYRIX_SETTINGS_FILE, outputFile);
  if (!result.ok()) {
    return result;
  }

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
  // Write themeName as fixed-length string
  outputFile.write(reinterpret_cast<const uint8_t*>(themeName), sizeof(themeName));
  outputFile.write(reinterpret_cast<const uint8_t*>(lastBookPath), sizeof(lastBookPath));
  serialization::writePod(outputFile, pendingTransition);
  serialization::writePod(outputFile, transitionReturnTo);
  serialization::writePod(outputFile, sunlightFadingFix);
  outputFile.close();

  Serial.printf("[%lu] [SET] Settings saved to file\n", millis());
  return Ok();
}

Result<void> Settings::load(drivers::Storage& storage) {
  FsFile inputFile;
  auto result = storage.openRead(PAPYRIX_SETTINGS_FILE, inputFile);
  if (!result.ok()) {
    return result;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version < MIN_SETTINGS_VERSION || version > SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [SET] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return ErrVoid(Error::UnsupportedVersion);
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  // Load settings that exist (support older files with fewer fields)
  // readPodValidated keeps default value if read value >= maxValue
  uint8_t settingsRead = 0;
  do {
    serialization::readPodValidated(inputFile, sleepScreen, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, textLayout, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, shortPwrBtn, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, statusBar, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, orientation, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, fontSize, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pagesPerRefresh, uint8_t(5));
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
    // Read themeName as fixed-length string
    inputFile.read(reinterpret_cast<uint8_t*>(themeName), sizeof(themeName));
    themeName[sizeof(themeName) - 1] = '\0';  // Ensure null-terminated
    if (++settingsRead >= fileSettingsCount) break;
    inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), sizeof(lastBookPath));
    lastBookPath[sizeof(lastBookPath) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pendingTransition, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, transitionReturnTo, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, sunlightFadingFix, uint8_t(1));
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  inputFile.close();
  Serial.printf("[%lu] [SET] Settings loaded from file\n", millis());
  return Ok();
}

int Settings::getReaderFontId(const Theme& theme) const {
  switch (fontSize) {
    case FontMedium:
      return FONT_MANAGER.getReaderFontId(theme.readerFontFamilyMedium, theme.readerFontIdMedium);
    case FontLarge:
      return FONT_MANAGER.getReaderFontId(theme.readerFontFamilyLarge, theme.readerFontIdLarge);
    default:
      return FONT_MANAGER.getReaderFontId(theme.readerFontFamilySmall, theme.readerFontId);
  }
}

RenderConfig Settings::getRenderConfig(const Theme& theme, uint16_t viewportWidth, uint16_t viewportHeight) const {
  return RenderConfig(getReaderFontId(theme), 0.95f, getIndentLevel(), getSpacingLevel(), paragraphAlignment,
                      static_cast<bool>(hyphenation), static_cast<bool>(showImages), viewportWidth, viewportHeight);
}

// Legacy methods that use SdMan directly (for early init before Core)
bool Settings::saveToFile() const {
  SdMan.mkdir(PAPYRIX_DIR);
  SdMan.mkdir(PAPYRIX_CACHE_DIR);

  FsFile outputFile;
  if (!SdMan.openFileForWrite("SET", PAPYRIX_SETTINGS_FILE, outputFile)) {
    return false;
  }

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
  outputFile.write(reinterpret_cast<const uint8_t*>(themeName), sizeof(themeName));
  outputFile.write(reinterpret_cast<const uint8_t*>(lastBookPath), sizeof(lastBookPath));
  serialization::writePod(outputFile, pendingTransition);
  serialization::writePod(outputFile, transitionReturnTo);
  serialization::writePod(outputFile, sunlightFadingFix);
  outputFile.close();

  Serial.printf("[%lu] [SET] Settings saved to file\n", millis());
  return true;
}

bool Settings::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("SET", PAPYRIX_SETTINGS_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version < MIN_SETTINGS_VERSION || version > SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [SET] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  uint8_t settingsRead = 0;
  do {
    serialization::readPodValidated(inputFile, sleepScreen, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, textLayout, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, shortPwrBtn, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, statusBar, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, orientation, uint8_t(4));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, fontSize, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pagesPerRefresh, uint8_t(5));
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
    inputFile.read(reinterpret_cast<uint8_t*>(themeName), sizeof(themeName));
    themeName[sizeof(themeName) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), sizeof(lastBookPath));
    lastBookPath[sizeof(lastBookPath) - 1] = '\0';
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, pendingTransition, uint8_t(3));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, transitionReturnTo, uint8_t(2));
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPodValidated(inputFile, sunlightFadingFix, uint8_t(1));
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  inputFile.close();
  Serial.printf("[%lu] [SET] Settings loaded from file\n", millis());
  return true;
}

}  // namespace papyrix
