#include "CalibreSettings.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <cstring>

#include "IniParser.h"
#include "config.h"

// Initialize the static instance
CalibreSettings CalibreSettings::instance;

namespace {

// Maximum lengths to prevent issues with malformed INI files
constexpr size_t MAX_NAME_LENGTH = 64;
constexpr size_t MAX_PASSWORD_LENGTH = 64;
}  // namespace

void CalibreSettings::createDefaultFile() {
  SdMan.mkdir(CONFIG_DIR);
  FsFile file;
  if (!SdMan.openFileForWrite("CAL", CONFIG_CALIBRE_FILE, file)) {
    Serial.printf("[%lu] [CAL] Failed to create default calibre.ini\n", millis());
    return;
  }

  file.println("# Calibre Wireless Device Configuration");
  file.println("#");
  file.println("# device_name: How your device appears in Calibre");
  file.println("# password: Optional password (leave empty for no password)");
  file.println("#           Must match the password set in Calibre's");
  file.println("#           Connect/Share > Start wireless device connection");
  file.println();
  file.println("[Settings]");
  file.println("device_name = Papyrix Reader");
  file.println("password = ");
  file.println();

  file.close();
  Serial.printf("[%lu] [CAL] Created default calibre.ini\n", millis());
}

bool CalibreSettings::loadFromFile() {
  // Reset to defaults
  strncpy(config.deviceName, "Papyrix Reader", sizeof(config.deviceName) - 1);
  config.deviceName[sizeof(config.deviceName) - 1] = '\0';
  config.password[0] = '\0';

  if (!SdMan.exists(CONFIG_CALIBRE_FILE)) {
    Serial.printf("[%lu] [CAL] No calibre.ini found, creating default\n", millis());
    createDefaultFile();
  }

  const bool parsed =
      IniParser::parseFile(CONFIG_CALIBRE_FILE, [&](const char* section, const char* key, const char* value) {
        if (strcmp(key, "device_name") == 0 && strlen(value) < MAX_NAME_LENGTH && strlen(value) > 0) {
          strncpy(config.deviceName, value, sizeof(config.deviceName) - 1);
          config.deviceName[sizeof(config.deviceName) - 1] = '\0';
        } else if (strcmp(key, "password") == 0 && strlen(value) < MAX_PASSWORD_LENGTH) {
          strncpy(config.password, value, sizeof(config.password) - 1);
          config.password[sizeof(config.password) - 1] = '\0';
        }

        return true;  // Continue parsing
      });

  loaded = parsed;
  Serial.printf("[%lu] [CAL] Loaded calibre.ini: device='%s', password=%s\n", millis(), config.deviceName,
                hasPassword() ? "set" : "none");
  return parsed;
}

bool CalibreSettings::saveToFile() {
  SdMan.mkdir(CONFIG_DIR);
  FsFile file;
  if (!SdMan.openFileForWrite("CAL", CONFIG_CALIBRE_FILE, file)) {
    Serial.printf("[%lu] [CAL] Failed to open calibre.ini for writing\n", millis());
    return false;
  }

  file.println("# Calibre Wireless Device Configuration");
  file.println("#");
  file.println("# device_name: How your device appears in Calibre");
  file.println("# password: Optional password (leave empty for no password)");
  file.println("#           Must match the password set in Calibre's");
  file.println("#           Connect/Share > Start wireless device connection");
  file.println();
  file.println("[Settings]");
  file.printf("device_name = %s\n", config.deviceName);
  file.printf("password = %s\n", config.password);
  file.println();

  file.close();
  Serial.printf("[%lu] [CAL] Saved calibre.ini\n", millis());
  return true;
}

void CalibreSettings::setDeviceName(const char* name) {
  if (name && strlen(name) < sizeof(config.deviceName)) {
    strncpy(config.deviceName, name, sizeof(config.deviceName) - 1);
    config.deviceName[sizeof(config.deviceName) - 1] = '\0';
  }
}

void CalibreSettings::setPassword(const char* pwd) {
  if (pwd && strlen(pwd) < sizeof(config.password)) {
    strncpy(config.password, pwd, sizeof(config.password) - 1);
    config.password[sizeof(config.password) - 1] = '\0';
  } else if (!pwd) {
    config.password[0] = '\0';
  }
}
