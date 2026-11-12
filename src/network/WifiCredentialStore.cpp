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
constexpr const char* WIFI_TMP_FILE = PAPYRIX_WIFI_FILE ".tmp";
constexpr const char* WIFI_BACKUP_FILE = PAPYRIX_WIFI_FILE ".bak";

// Keep the legacy key to read saved Wi-Fi credentials.
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
  if (!SdMan.openFileForWrite("WCS", WIFI_TMP_FILE, file)) {
    LOG_ERR(TAG, "Failed to open wifi.bin for write");
    return false;
  }

  bool ok = file.write(WIFI_FILE_VERSION) == 1 && file.write(count_) == 1;

  for (int i = 0; i < count_; i++) {
    // Write SSID length + data
    uint8_t ssidLen = strlen(credentials_[i].ssid);
    ok = file.write(ssidLen) == 1 &&
         file.write(reinterpret_cast<const uint8_t*>(credentials_[i].ssid), ssidLen) == ssidLen && ok;

    // Write password length + obfuscated data
    uint8_t pwdLen = strlen(credentials_[i].password);
    ok = file.write(pwdLen) == 1 && ok;

    char obfuscated[65];
    strncpy(obfuscated, credentials_[i].password, sizeof(obfuscated) - 1);
    obfuscated[sizeof(obfuscated) - 1] = '\0';
    obfuscate(obfuscated, pwdLen);
    ok = file.write(reinterpret_cast<const uint8_t*>(obfuscated), pwdLen) == pwdLen && ok;
  }

  ok = file.sync() && ok;
  file.close();
  if (!ok) {
    SdMan.remove(WIFI_TMP_FILE);
    LOG_ERR(TAG, "Failed to write wifi.bin");
    return false;
  }
  const bool hadFile = SdMan.exists(PAPYRIX_WIFI_FILE);
  if (hadFile) {
    SdMan.remove(WIFI_BACKUP_FILE);
    if (!SdMan.rename(PAPYRIX_WIFI_FILE, WIFI_BACKUP_FILE)) {
      SdMan.remove(WIFI_TMP_FILE);
      return false;
    }
  }
  if (!SdMan.rename(WIFI_TMP_FILE, PAPYRIX_WIFI_FILE)) {
    if (hadFile) SdMan.rename(WIFI_BACKUP_FILE, PAPYRIX_WIFI_FILE);
    SdMan.remove(WIFI_TMP_FILE);
    return false;
  }
  SdMan.remove(WIFI_BACKUP_FILE);
  LOG_INF(TAG, "Saved %d credentials", count_);
  return true;
}

bool WifiCredentialStore::loadFromFile() {
  FsFile file;
  if (!SdMan.openFileForRead("WCS", PAPYRIX_WIFI_FILE, file) && !SdMan.openFileForRead("WCS", WIFI_BACKUP_FILE, file)) {
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
  if (countByte < 0 || countByte > MAX_NETWORKS) {
    LOG_ERR(TAG, "Invalid credential count: %d", countByte);
    file.close();
    return false;
  }

  WifiCredential loaded[MAX_NETWORKS] = {};
  for (int i = 0; i < countByte; i++) {
    const int ssidLen = file.read();
    if (ssidLen <= 0 || ssidLen > 32 || file.read(reinterpret_cast<uint8_t*>(loaded[i].ssid), ssidLen) != ssidLen ||
        memchr(loaded[i].ssid, '\0', ssidLen)) {
      LOG_ERR(TAG, "Invalid SSID data for credential %d", i);
      file.close();
      return false;
    }

    const int pwdLen = file.read();
    if (pwdLen < 0 || pwdLen > 64 || file.read(reinterpret_cast<uint8_t*>(loaded[i].password), pwdLen) != pwdLen) {
      LOG_ERR(TAG, "Invalid password data for credential %d", i);
      file.close();
      return false;
    }
    obfuscate(loaded[i].password, pwdLen);
    if (memchr(loaded[i].password, '\0', pwdLen)) {
      LOG_ERR(TAG, "Invalid password bytes for credential %d", i);
      file.close();
      return false;
    }
  }

  memcpy(credentials_, loaded, countByte * sizeof(WifiCredential));
  count_ = static_cast<uint8_t>(countByte);

  file.close();
  LOG_INF(TAG, "Loaded %d credentials", count_);
  return true;
}

bool WifiCredentialStore::addCredential(const char* ssid, const char* password) {
  if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 64) return false;
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, ssid) != 0) continue;
    const WifiCredential previous = credentials_[i];
    strncpy(credentials_[i].password, password, sizeof(credentials_[i].password) - 1);
    credentials_[i].password[sizeof(credentials_[i].password) - 1] = '\0';
    if (saveToFile()) return true;
    credentials_[i] = previous;
    return false;
  }

  if (count_ >= MAX_NETWORKS) return false;
  strncpy(credentials_[count_].ssid, ssid, sizeof(credentials_[count_].ssid) - 1);
  credentials_[count_].ssid[sizeof(credentials_[count_].ssid) - 1] = '\0';
  strncpy(credentials_[count_].password, password, sizeof(credentials_[count_].password) - 1);
  credentials_[count_].password[sizeof(credentials_[count_].password) - 1] = '\0';
  count_++;
  if (saveToFile()) return true;
  memset(&credentials_[--count_], 0, sizeof(WifiCredential));
  return false;
}

bool WifiCredentialStore::updateCredential(const char* currentSsid, const char* newSsid, const char* password) {
  if (!currentSsid || !newSsid || !password || !newSsid[0] || strlen(newSsid) > 32 || strlen(password) > 64)
    return false;
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, currentSsid) != 0) continue;
    if (strcmp(currentSsid, newSsid) != 0 && hasSavedCredential(newSsid)) return false;
    const WifiCredential previous = credentials_[i];
    strncpy(credentials_[i].ssid, newSsid, sizeof(credentials_[i].ssid));
    strncpy(credentials_[i].password, password, sizeof(credentials_[i].password));
    if (saveToFile()) return true;
    credentials_[i] = previous;
    return false;
  }
  return false;
}

bool WifiCredentialStore::removeCredential(const char* ssid) {
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, ssid) != 0) continue;
    const WifiCredential removed = credentials_[i];
    for (int j = i; j < count_ - 1; j++) credentials_[j] = credentials_[j + 1];
    count_--;
    if (saveToFile()) {
      memset(&credentials_[count_], 0, sizeof(WifiCredential));
      return true;
    }
    for (int j = count_; j > i; j--) credentials_[j] = credentials_[j - 1];
    credentials_[i] = removed;
    count_++;
    return false;
  }
  return false;
}

bool WifiCredentialStore::moveCredential(const char* ssid, int direction) {
  if (direction != -1 && direction != 1) return false;
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, ssid) != 0) continue;
    const int next = i + direction;
    if (next < 0 || next >= count_) return false;
    const WifiCredential previous = credentials_[i];
    credentials_[i] = credentials_[next];
    credentials_[next] = previous;
    if (saveToFile()) return true;
    credentials_[next] = credentials_[i];
    credentials_[i] = previous;
    return false;
  }
  return false;
}

bool WifiCredentialStore::promoteCredential(const char* ssid) {
  for (int i = 0; i < count_; i++) {
    if (strcmp(credentials_[i].ssid, ssid) != 0) continue;
    if (i == 0) return true;
    const WifiCredential connected = credentials_[i];
    for (int j = i; j > 0; j--) credentials_[j] = credentials_[j - 1];
    credentials_[0] = connected;
    if (saveToFile()) return true;
    for (int j = 0; j < i; j++) credentials_[j] = credentials_[j + 1];
    credentials_[i] = connected;
    return false;
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
