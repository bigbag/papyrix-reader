#include "OpdsServerStore.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <cstring>

#include "IniParser.h"
#include "config.h"

// Initialize the static instance
OpdsServerStore OpdsServerStore::instance;

namespace {

// Maximum lengths to prevent heap exhaustion from malformed INI files
constexpr size_t MAX_URL_LENGTH = 256;
constexpr size_t MAX_NAME_LENGTH = 64;
constexpr size_t MAX_CREDENTIAL_LENGTH = 128;
}  // namespace

void OpdsServerStore::createDefaultFile() {
  SdMan.mkdir(CONFIG_DIR);
  FsFile file;
  if (!SdMan.openFileForWrite("OSS", CONFIG_OPDS_FILE, file)) {
    Serial.printf("[%lu] [OSS] Failed to create default opds.ini\n", millis());
    return;
  }

  file.println("# OPDS Server Configuration");
  file.println("# Add your servers below. Section name = display name.");
  file.println("#");
  file.println("# Example:");
  file.println("# [My Calibre Server]");
  file.println("# url = http://192.168.1.100:8080/opds");
  file.println("# username = myuser");
  file.println("# password = mypassword");
  file.println();
  file.println("[Project Gutenberg]");
  file.println("url = https://m.gutenberg.org/ebooks.opds/");
  file.println();

  file.close();
  Serial.printf("[%lu] [OSS] Created default opds.ini with Project Gutenberg\n", millis());
}

bool OpdsServerStore::loadFromFile() {
  servers.clear();

  if (!SdMan.exists(CONFIG_OPDS_FILE)) {
    Serial.printf("[%lu] [OSS] No opds.ini found, creating default\n", millis());
    createDefaultFile();
  }

  OpdsServerConfig current;
  std::string currentSection;

  const bool parsed =
      IniParser::parseFile(CONFIG_OPDS_FILE, [&](const char* section, const char* key, const char* value) {
        // Check if we're in a new section
        if (currentSection != section) {
          // Save previous server if it had a URL
          if (!current.url.empty() && servers.size() < MAX_SERVERS) {
            servers.push_back(current);
          }
          // Start new server config
          current = OpdsServerConfig();
          if (strlen(section) < MAX_NAME_LENGTH) {
            current.name = section;
          }
          currentSection = section;
        }

        // Parse key-value pairs with length limits
        if (strcmp(key, "url") == 0 && strlen(value) < MAX_URL_LENGTH) {
          current.url = value;
        } else if (strcmp(key, "username") == 0 && strlen(value) < MAX_CREDENTIAL_LENGTH) {
          current.username = value;
        } else if (strcmp(key, "password") == 0 && strlen(value) < MAX_CREDENTIAL_LENGTH) {
          current.password = value;
        }

        return servers.size() < MAX_SERVERS;  // Continue parsing if under limit
      });

  // Don't forget the last server
  if (!current.url.empty() && servers.size() < MAX_SERVERS) {
    servers.push_back(current);
  }

  Serial.printf("[%lu] [OSS] Loaded %zu OPDS servers from opds.ini\n", millis(), servers.size());
  return parsed;
}

const OpdsServerConfig* OpdsServerStore::getServer(size_t index) const {
  if (index >= servers.size()) {
    return nullptr;
  }
  return &servers[index];
}
