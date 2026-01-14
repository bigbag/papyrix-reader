#include "CalibreDeviceServer.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <time.h>

#include "CalibreProtocol.h"
#include "calibre/CalibreSettings.h"

namespace {
// Books directory
constexpr char BOOKS_DIR[] = "/Books";

// Buffer for streaming
constexpr size_t STREAM_BUFFER_SIZE = 4096;
}  // namespace

CalibreDeviceServer::CalibreDeviceServer() : server(9090) {}

CalibreDeviceServer::~CalibreDeviceServer() { stop(); }

bool CalibreDeviceServer::begin(uint16_t port) {
  if (running) {
    return true;
  }

  tcpPort = port;

  // Load settings
  CALIBRE_SETTINGS.loadFromFile();

  // Setup UDP discovery listener
  if (!setupUdpListener()) {
    Serial.println("[CAL] Failed to setup UDP listener");
    // Continue anyway - Calibre can still connect directly
  }

  // Start TCP server
  server = WiFiServer(tcpPort);
  server.begin();

  running = true;
  reportStatus("Waiting for Calibre...");
  Serial.printf("[CAL] Server started on port %u (UDP: %u)\n", tcpPort, udpPort);

  return true;
}

void CalibreDeviceServer::stop() {
  if (!running) {
    return;
  }

  if (client) {
    client.stop();
  }

  server.stop();
  udp.stop();

  running = false;
  receiving = false;
  Serial.println("[CAL] Server stopped");
}

void CalibreDeviceServer::loop() {
  if (!running) {
    return;
  }

  // Handle UDP discovery
  handleUdpDiscovery();

  // Check for new client connections
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();
    if (newClient) {
      if (client) {
        client.stop();
      }
      client = newClient;
      handleNewClient();
    }
  }

  // Handle messages from connected client
  if (client && client.connected() && client.available()) {
    handleClientMessage();
  }
}

bool CalibreDeviceServer::setupUdpListener() {
  // Try each discovery port until one works
  for (size_t i = 0; i < CalibreProtocol::UDP_PORT_COUNT; i++) {
    uint16_t port = CalibreProtocol::UDP_PORTS[i];
    if (udp.begin(port)) {
      udpPort = port;
      Serial.printf("[CAL] UDP listening on port %u\n", port);
      return true;
    }
  }

  Serial.println("[CAL] Could not bind to any UDP discovery port");
  return false;
}

void CalibreDeviceServer::handleUdpDiscovery() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  // Read the broadcast message (Calibre sends various discovery messages)
  char buffer[64];
  int len = udp.read(buffer, sizeof(buffer) - 1);
  if (len > 0) {
    buffer[len] = '\0';
    Serial.printf("[CAL] UDP discovery from %s: %s\n", udp.remoteIP().toString().c_str(), buffer);
  }

  // Respond with our presence
  // Format: "calibre wireless device client (on <IP>);<content_port>,<tcp_port>"
  char response[128];
  snprintf(response, sizeof(response), "calibre wireless device client (on %s);80,%u",
           WiFi.localIP().toString().c_str(), tcpPort);

  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.print(response);
  udp.endPacket();

  Serial.printf("[CAL] Sent discovery response: %s\n", response);
}

void CalibreDeviceServer::handleNewClient() {
  Serial.printf("[CAL] Client connected from %s\n", client.remoteIP().toString().c_str());
  reportStatus("Client connected");

  // Generate challenge for password auth
  currentChallenge = generateChallenge();
}

void CalibreDeviceServer::handleClientMessage() {
  uint8_t opcode;
  std::string data;

  if (!CalibreProtocol::parseMessage(client, opcode, data)) {
    return;
  }

  Serial.printf("[CAL] Received opcode %u, data length %zu\n", opcode, data.size());

  switch (opcode) {
    case CalibreProtocol::OP_NOOP:
      handleNoop();
      break;

    case CalibreProtocol::OP_GET_INIT_INFO:
      handleGetInitInfo(data);
      break;

    case CalibreProtocol::OP_TOTAL_SPACE:
      handleTotalSpace();
      break;

    case CalibreProtocol::OP_FREE_SPACE:
      handleFreeSpace();
      break;

    case CalibreProtocol::OP_GET_BOOK_COUNT:
      handleGetBookCount();
      break;

    case CalibreProtocol::OP_SEND_BOOKLISTS:
      handleSendBooklists(data);
      break;

    case CalibreProtocol::OP_SEND_BOOK:
      handleSendBook(data);
      break;

    case CalibreProtocol::OP_DELETE_BOOK:
      handleDeleteBook(data);
      break;

    default:
      Serial.printf("[CAL] Unknown opcode: %u\n", opcode);
      // Send error response
      CalibreProtocol::sendMessage(client, CalibreProtocol::OP_ERROR, "{\"message\": \"Unknown opcode\"}");
      break;
  }
}

void CalibreDeviceServer::handleGetInitInfo(const std::string& data) {
  Serial.println("[CAL] Handling GET_INITIALIZATION_INFO");

  // Check password if required
  std::string passwordHash;
  if (CALIBRE_SETTINGS.hasPassword()) {
    passwordHash = CalibreProtocol::computePasswordHash(CALIBRE_SETTINGS.getPassword(), currentChallenge);
  }

  // Build initialization response
  std::string deviceName = CalibreProtocol::escapeJsonString(CALIBRE_SETTINGS.getDeviceName());

  char response[1024];
  snprintf(response, sizeof(response),
           "{"
           "\"versionOK\": true, "
           "\"maxBookContentPacketLen\": %zu, "
           "\"acceptedExtensions\": [\"epub\"], "
           "\"canStreamBooks\": true, "
           "\"canStreamMetadata\": true, "
           "\"canReceiveBookBinary\": true, "
           "\"canDeleteMultipleBooks\": true, "
           "\"canUseCachedMetadata\": false, "
           "\"cacheUsesLpaths\": false, "
           "\"coverHeight\": 200, "
           "\"deviceKind\": \"ESP32 E-Reader\", "
           "\"deviceName\": \"%s\", "
           "\"extensionPathLengths\": {}, "
           "\"passwordHash\": \"%s\", "
           "\"currentLibraryName\": \"\", "
           "\"currentLibraryUUID\": \"\", "
           "\"ccVersionNumber\": %d"
           "}",
           CalibreProtocol::MAX_BOOK_PACKET_LEN, deviceName.c_str(), passwordHash.c_str(),
           CalibreProtocol::PROTOCOL_VERSION);

  CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, response);
  reportStatus("Connected to Calibre");
}

void CalibreDeviceServer::handleNoop() {
  // Respond with NOOP to keep connection alive
  CalibreProtocol::sendMessage(client, CalibreProtocol::OP_NOOP, "{}");
}

void CalibreDeviceServer::handleTotalSpace() {
  // Return a reasonable default - actual SD card space is hard to query
  // Most SD cards are 2-32GB, we'll report 4GB as a safe estimate
  uint64_t total = 4ULL * 1024 * 1024 * 1024;  // 4GB

  char response[64];
  snprintf(response, sizeof(response), "{\"total_space_on_device\": %llu}", static_cast<unsigned long long>(total));
  CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, response);
}

void CalibreDeviceServer::handleFreeSpace() {
  // Return a reasonable estimate for free space
  // Without direct SdFat volume access, we estimate 2GB free
  uint64_t free = 2ULL * 1024 * 1024 * 1024;  // 2GB free

  char response[64];
  snprintf(response, sizeof(response), "{\"free_space_on_device\": %llu}", static_cast<unsigned long long>(free));
  CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, response);
}

void CalibreDeviceServer::handleGetBookCount() {
  std::vector<CalibreBookInfo> books = scanBooks();
  char response[64];
  snprintf(response, sizeof(response), "{\"count\": %zu}", books.size());
  CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, response);
}

void CalibreDeviceServer::handleSendBooklists(const std::string& data) {
  Serial.println("[CAL] Sending booklists to Calibre");
  reportStatus("Syncing library...");

  std::vector<CalibreBookInfo> books = scanBooks();
  std::string booklistJson = buildBooklistJson(books);

  CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, booklistJson.c_str());
  reportStatus("Connected to Calibre");
}

void CalibreDeviceServer::handleSendBook(const std::string& data) {
  // Extract book info from JSON
  std::string lpath = CalibreProtocol::extractJsonString(data, "lpath");
  int64_t length = CalibreProtocol::extractJsonInt(data, "length");
  std::string title = CalibreProtocol::extractJsonString(data, "title");

  if (lpath.empty()) {
    // Try to get title from metadata for display
    std::string metadataStr = CalibreProtocol::extractJsonString(data, "metadata");
    if (!metadataStr.empty()) {
      title = CalibreProtocol::extractJsonString(metadataStr, "title");
    }
  }

  if (title.empty()) {
    title = "Unknown";
  }

  Serial.printf("[CAL] Receiving book: %s (%lld bytes)\n", title.c_str(), length);
  reportStatus("Receiving book...");

  // Ensure Books directory exists
  if (!SdMan.exists(BOOKS_DIR)) {
    SdMan.mkdir(BOOKS_DIR);
  }

  // Build destination path
  std::string destPath;
  if (!lpath.empty()) {
    // Use lpath if provided (relative path on device)
    destPath = "/" + lpath;
    // Ensure parent directories exist
    size_t lastSlash = destPath.rfind('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
      std::string parentDir = destPath.substr(0, lastSlash);
      if (!SdMan.exists(parentDir.c_str())) {
        SdMan.mkdir(parentDir.c_str());
      }
    }
  } else {
    // Create path from title
    std::string safeName = sanitizeFilename(title);
    destPath = std::string(BOOKS_DIR) + "/" + safeName + ".epub";
  }

  currentBookPath = destPath;
  currentBookTitle = title;
  currentBookSize = static_cast<size_t>(length);
  currentBookReceived = 0;
  receiving = true;

  // Send OK to indicate we're ready to receive the binary data
  CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, "{\"willStreamBinary\": true}");

  // Stream the book to file
  if (streamBookToFile(currentBookSize, destPath)) {
    // Send BOOK_DONE
    CalibreProtocol::sendMessage(client, CalibreProtocol::OP_BOOK_DONE, "{}");
    Serial.printf("[CAL] Book saved: %s\n", destPath.c_str());
    reportStatus("Book received!");

    if (onBookReceived) {
      onBookReceived(destPath.c_str());
    }
  } else {
    Serial.println("[CAL] Failed to save book");
    CalibreProtocol::sendMessage(client, CalibreProtocol::OP_ERROR, "{\"message\": \"Failed to save book\"}");
    reportStatus("Transfer failed");
  }

  receiving = false;
}

void CalibreDeviceServer::handleDeleteBook(const std::string& data) {
  std::string lpath = CalibreProtocol::extractJsonString(data, "lpath");
  if (lpath.empty()) {
    CalibreProtocol::sendMessage(client, CalibreProtocol::OP_ERROR, "{\"message\": \"No lpath provided\"}");
    return;
  }

  std::string fullPath = "/" + lpath;
  Serial.printf("[CAL] Deleting book: %s\n", fullPath.c_str());
  reportStatus("Deleting book...");

  if (SdMan.exists(fullPath.c_str())) {
    if (SdMan.remove(fullPath.c_str())) {
      CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, "{}");
      Serial.printf("[CAL] Deleted: %s\n", fullPath.c_str());
      reportStatus("Book deleted");

      if (onBookDeleted) {
        onBookDeleted(fullPath.c_str());
      }
    } else {
      CalibreProtocol::sendMessage(client, CalibreProtocol::OP_ERROR, "{\"message\": \"Failed to delete file\"}");
      reportStatus("Delete failed");
    }
  } else {
    // File doesn't exist - still report success to Calibre
    CalibreProtocol::sendMessage(client, CalibreProtocol::OP_OK, "{}");
  }
}

std::vector<CalibreBookInfo> CalibreDeviceServer::scanBooks() {
  std::vector<CalibreBookInfo> books;

  // Ensure Books directory exists
  if (!SdMan.exists(BOOKS_DIR)) {
    SdMan.mkdir(BOOKS_DIR);
    Serial.println("[CAL] Created Books directory");
    return books;
  }

  FsFile dir = SdMan.open(BOOKS_DIR);
  if (!dir || !dir.isDirectory()) {
    Serial.println("[CAL] Books directory not found");
    return books;
  }

  FsFile entry;
  while (entry.openNext(&dir)) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    char name[256];
    entry.getName(name, sizeof(name));

    // Check if it's an EPUB
    size_t nameLen = strlen(name);
    if (nameLen > 5 && strcasecmp(name + nameLen - 5, ".epub") == 0) {
      CalibreBookInfo info;
      info.lpath = std::string("Books/") + name;
      info.size = entry.fileSize();

      // Extract title from filename (remove .epub extension)
      info.title = std::string(name, nameLen - 5);
      info.author = "";  // Could parse EPUB metadata later

      books.push_back(info);
    }

    entry.close();
  }

  dir.close();
  Serial.printf("[CAL] Found %zu books\n", books.size());
  return books;
}

std::string CalibreDeviceServer::buildBooklistJson(const std::vector<CalibreBookInfo>& books) {
  std::string json = "[";

  for (size_t i = 0; i < books.size(); i++) {
    if (i > 0) {
      json += ", ";
    }

    std::string escapedTitle = CalibreProtocol::escapeJsonString(books[i].title);
    std::string escapedAuthor = CalibreProtocol::escapeJsonString(books[i].author);
    std::string escapedPath = CalibreProtocol::escapeJsonString(books[i].lpath);

    char entry[512];
    snprintf(entry, sizeof(entry),
             "{"
             "\"lpath\": \"%s\", "
             "\"title\": \"%s\", "
             "\"authors\": [\"%s\"], "
             "\"size\": %zu"
             "}",
             escapedPath.c_str(), escapedTitle.c_str(), escapedAuthor.c_str(), books[i].size);

    json += entry;
  }

  json += "]";
  return json;
}

std::string CalibreDeviceServer::sanitizeFilename(const std::string& name) {
  std::string result;
  result.reserve(name.size());

  for (char c : name) {
    // Replace invalid filename characters
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
        c < 32) {
      result += '_';
    } else {
      result += c;
    }
  }

  // Trim spaces and dots from ends
  size_t start = 0;
  size_t end = result.size();
  while (start < end && (result[start] == ' ' || result[start] == '.')) {
    start++;
  }
  while (end > start && (result[end - 1] == ' ' || result[end - 1] == '.')) {
    end--;
  }

  if (start >= end) {
    return "Untitled";
  }

  return result.substr(start, end - start);
}

bool CalibreDeviceServer::streamBookToFile(size_t expectedSize, const std::string& destPath) {
  FsFile file;
  if (!SdMan.openFileForWrite("CAL", destPath.c_str(), file)) {
    Serial.printf("[CAL] Failed to open file for writing: %s\n", destPath.c_str());
    return false;
  }

  uint8_t buffer[STREAM_BUFFER_SIZE];
  size_t received = 0;
  unsigned long lastProgressTime = 0;
  unsigned long timeout = 30000;  // 30 second timeout
  unsigned long lastDataTime = millis();

  while (received < expectedSize) {
    if (!client.connected()) {
      Serial.println("[CAL] Client disconnected during transfer");
      file.close();
      SdMan.remove(destPath.c_str());
      return false;
    }

    size_t available = client.available();
    if (available == 0) {
      if (millis() - lastDataTime > timeout) {
        Serial.println("[CAL] Transfer timeout");
        file.close();
        SdMan.remove(destPath.c_str());
        return false;
      }
      delay(1);
      continue;
    }

    lastDataTime = millis();
    size_t toRead = std::min(available, std::min(STREAM_BUFFER_SIZE, expectedSize - received));
    size_t bytesRead = client.read(buffer, toRead);

    if (bytesRead > 0) {
      size_t written = file.write(buffer, bytesRead);
      if (written != bytesRead) {
        Serial.println("[CAL] SD card write error");
        file.close();
        SdMan.remove(destPath.c_str());
        return false;
      }

      received += bytesRead;

      // Report progress (but not too often)
      if (millis() - lastProgressTime > 250) {
        lastProgressTime = millis();
        reportProgress(currentBookTitle.c_str(), received, expectedSize);
      }
    }
  }

  file.close();

  // Final progress update
  reportProgress(currentBookTitle.c_str(), received, expectedSize);

  return true;
}

void CalibreDeviceServer::reportStatus(const char* status) {
  if (onStatus) {
    onStatus(status);
  }
}

void CalibreDeviceServer::reportProgress(const char* title, size_t received, size_t total) {
  if (onProgress) {
    onProgress(title, received, total);
  }
}

std::string CalibreDeviceServer::generateChallenge() {
  // Generate ISO 8601 timestamp as challenge
  time_t now = time(nullptr);
  struct tm tm_info;
  localtime_r(&now, &tm_info);

  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm_info);

  return std::string(buffer);
}
