#include "WifiCredentialStore.h"

#include <Arduino.h>
#include <Logging.h>
#include <SDCardManager.h>

#include <cstring>

#include "../config.h"

#define TAG "WIFI_CRED"

namespace papyrix {

namespace {
constexpr uint8_t WIFI_FILE_VERSION = 1;

// Obfuscation key - "Papyrix" in ASCII (not cryptographic, just prevents casual reading)
constexpr uint8_t OBFUSCATION_KEY[] = {0x50, 0x61, 0x70, 0x79, 0x72, 0x69, 0x78};
constexpr size_t KEY_LENGTH = sizeof(OBFUSCATION_KEY);
}  // namespace

WifiCredentialStore& WifiCredentialStore::getInstance() {
  static WifiCredentialStore instance;
  return instance;
}

void WifiCredentialStore::obfuscate(char* data, size_t len) const {
  for (size_t i = 0; i < len; i++) {
    data[i] ^= OBFUSCATION_KEY[i % KEY_LENGTH];
  }
}

bool WifiCredentialStore::saveToFile() const {
  SdMan.mkdir(PAPYRIX_DIR);

  FsFile file;
  if (!SdMan.openFileForWrite("WCS", PAPYRIX_WIFI_FILE, file)) {
    LOG_ERR(TAG, "Failed to open wifi.bin for write");
    return false;
  }

  file.write(WIFI_FILE_VERSION);
  file.write(count_);

  for (int i = 0; i < count_; i++) {
    // Write SSID length + data
    uint8_t ssidLen = strlen(credentials_[i].ssid);
    file.write(ssidLen);
    file.write(reinterpret_cast<const uint8_t*>(credentials_[i].ssid), ssidLen);

    // Write password length + obfuscated data
    uint8_t pwdLen = strlen(credentials_[i].password);
    file.write(pwdLen);

    char obfuscated[65];
    strncpy(obfuscated, credentials_[i].password, sizeof(obfuscated) - 1);
    obfuscated[sizeof(obfuscated) - 1] = '\0';
    obfuscate(obfuscated, pwdLen);
    file.write(reinterpret_cast<const uint8_t*>(obfuscated), pwdLen);
  }

  file.close();
  LOG_INF(TAG, "Saved %d credentials", count_);
  return true;
}

bool WifiCredentialStore::loadFromFile() {
  FsFile file;
  if (!SdMan.openFileForRead("WCS", PAPYRIX_WIFI_FILE, file)) {
    return false;
  }

  uint8_t version = file.read();
  if (version != WIFI_FILE_VERSION) {
    LOG_ERR(TAG, "Unknown file version: %d", version);
    file.close();
    return false;
  }

  if (file.available() < 1) {
    LOG_ERR(TAG, "File truncated: missing count");
    file.close();
    return false;
  }

  int countByte = file.read();
  if (countByte < 0) {
    LOG_ERR(TAG, "Failed to read count");
    file.close();
    return false;
  }
  count_ = static_cast<uint8_t>(countByte);
  if (count_ > MAX_NETWORKS) {
    count_ = MAX_NETWORKS;
  }

  for (int i = 0; i < count_; i++) {
    // Read SSID length
    int ssidLenByte = file.read();
    if (ssidLenByte < 0) {
      LOG_ERR(TAG, "Failed to read SSID length for credential %d", i);
      count_ = i;
      break;
    }
    uint8_t ssidLen = static_cast<uint8_t>(ssidLenByte);
    if (ssidLen > 32) ssidLen = 32;

    // Read SSID data
    int ssidRead = file.read(reinterpret_cast<uint8_t*>(credentials_[i].ssid), ssidLen);
    if (ssidRead < 0) ssidRead = 0;
    credentials_[i].ssid[ssidRead] = '\0';

    // Read password length
    int pwdLenByte = file.read();
    if (pwdLenByte < 0) {
      LOG_ERR(TAG, "Failed to read password length for credential %d", i);
      count_ = i;
      break;
    }
    uint8_t pwdLen = static_cast<uint8_t>(pwdLenByte);
    if (pwdLen > 64) pwdLen = 64;

    // Read and deobfuscate password
    int pwdRead = file.read(reinterpret_cast<uint8_t*>(credentials_[i].password), pwdLen);
    if (pwdRead < 0) pwdRead = 0;
    credentials_[i].password[pwdRead] = '\0';
    obfuscate(credentials_[i].password, pwdRead);
  }

  file.close();
  LOG_INF(TAG, "Loaded %d credentials", count_);
  return true;
}

bool WifiCredentialStore::addCredential(const char* ssid, const char* password) {
  // Check if SSID already exists and update it
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, ssid) == 0) {
      strncpy(credentials_[i].password, password, sizeof(credentials_[i].password) - 1);
      credentials_[i].password[sizeof(credentials_[i].password) - 1] = '\0';
      LOG_INF(TAG, "Updated credentials for: %s", ssid);
      return saveToFile();
    }
  }

  // Check limit
  if (count_ >= MAX_NETWORKS) {
    LOG_ERR(TAG, "Cannot add more networks, limit reached");
    return false;
  }

  // Add new credential
  strncpy(credentials_[count_].ssid, ssid, sizeof(credentials_[count_].ssid) - 1);
  credentials_[count_].ssid[sizeof(credentials_[count_].ssid) - 1] = '\0';
  strncpy(credentials_[count_].password, password, sizeof(credentials_[count_].password) - 1);
  credentials_[count_].password[sizeof(credentials_[count_].password) - 1] = '\0';
  count_++;

  LOG_INF(TAG, "Added credentials for: %s", ssid);
  return saveToFile();
}

bool WifiCredentialStore::removeCredential(const char* ssid) {
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, ssid) == 0) {
      // Shift remaining credentials down
      for (int j = i; j < count_ - 1; j++) {
        credentials_[j] = credentials_[j + 1];
      }
      count_--;
      LOG_INF(TAG, "Removed credentials for: %s", ssid);
      return saveToFile();
    }
  }
  return false;
}

const WifiCredential* WifiCredentialStore::findCredential(const char* ssid) const {
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, ssid) == 0) {
      return &credentials_[i];
    }
  }
  return nullptr;
}

bool WifiCredentialStore::hasSavedCredential(const char* ssid) const { return findCredential(ssid) != nullptr; }

void WifiCredentialStore::clearAll() {
  count_ = 0;
  saveToFile();
  LOG_INF(TAG, "Cleared all credentials");
}

}  // namespace papyrix
