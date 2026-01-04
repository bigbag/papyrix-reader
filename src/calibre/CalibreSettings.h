#pragma once

#include <cstdint>
#include <string>

/**
 * Configuration for Calibre Wireless Device connection.
 */
struct CalibreConfig {
  char deviceName[64] = "Papyrix Reader";
  char password[64] = "";  // Empty = no password required
};

/**
 * Singleton class for reading/writing Calibre settings from SD card.
 * Settings are stored in /calibre.ini as user-editable INI file.
 */
class CalibreSettings {
 private:
  static CalibreSettings instance;
  CalibreConfig config;
  bool loaded = false;

  // Private constructor for singleton
  CalibreSettings() = default;

  void createDefaultFile();

 public:
  // Delete copy constructor and assignment
  CalibreSettings(const CalibreSettings&) = delete;
  CalibreSettings& operator=(const CalibreSettings&) = delete;

  // Get singleton instance
  static CalibreSettings& getInstance() { return instance; }

  // Load from SD card (creates default if missing)
  bool loadFromFile();

  // Save current config to SD card
  bool saveToFile();

  // Accessors
  const CalibreConfig& getConfig() const { return config; }
  const char* getDeviceName() const { return config.deviceName; }
  const char* getPassword() const { return config.password; }
  bool hasPassword() const { return config.password[0] != '\0'; }

  // Mutators
  void setDeviceName(const char* name);
  void setPassword(const char* pwd);
};

// Helper macro to access settings
#define CALIBRE_SETTINGS CalibreSettings::getInstance()
