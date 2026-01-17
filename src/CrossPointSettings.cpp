#include "CrossPointSettings.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "config.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

namespace {
// Version 3: Removed showBookDetails (now always enabled)
constexpr uint8_t SETTINGS_FILE_VERSION = 3;
// Increment this when adding new persisted settings fields
constexpr uint8_t SETTINGS_COUNT = 13;
}  // namespace

bool CrossPointSettings::saveToFile() const {
  // Make sure the directory exists
  SdMan.mkdir(PAPYRIX_DIR);

  FsFile outputFile;
  if (!SdMan.openFileForWrite("CPS", PAPYRIX_SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, extraParagraphSpacing);
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
  // Write themeName as fixed-length string
  outputFile.write(reinterpret_cast<const uint8_t*>(themeName), sizeof(themeName));
  outputFile.close();

  Serial.printf("[%lu] [CPS] Settings saved to file\n", millis());
  return true;
}

bool CrossPointSettings::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("CPS", PAPYRIX_SETTINGS_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version != SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  // load settings that exist (support older files with fewer fields)
  uint8_t settingsRead = 0;
  do {
    serialization::readPod(inputFile, sleepScreen);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, shortPwrBtn);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, statusBar);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, orientation);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, fontSize);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, pagesPerRefresh);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, sideButtonLayout);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, autoSleepMinutes);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, paragraphAlignment);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, hyphenation);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;
    // Read themeName as fixed-length string
    inputFile.read(reinterpret_cast<uint8_t*>(themeName), sizeof(themeName));
    themeName[sizeof(themeName) - 1] = '\0';  // Ensure null-terminated
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  inputFile.close();
  Serial.printf("[%lu] [CPS] Settings loaded from file\n", millis());
  return true;
}
