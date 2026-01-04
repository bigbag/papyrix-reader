#pragma once

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>

#include <functional>
#include <string>
#include <vector>

/**
 * Book metadata for sending to Calibre.
 */
struct CalibreBookInfo {
  std::string lpath;    // Logical path on device (e.g., "Books/Title.epub")
  std::string title;
  std::string author;
  size_t size;
};

/**
 * Calibre Wireless Device server.
 *
 * Implements the Calibre Smart Device protocol:
 * - UDP discovery (responds to Calibre broadcasts)
 * - TCP server for JSON protocol
 * - Book transfer and management
 */
class CalibreDeviceServer {
 public:
  // Callback types
  using StatusCallback = std::function<void(const char* status)>;
  using ProgressCallback = std::function<void(const char* title, size_t received, size_t total)>;
  using BookReceivedCallback = std::function<void(const char* path)>;
  using BookDeletedCallback = std::function<void(const char* path)>;

  CalibreDeviceServer();
  ~CalibreDeviceServer();

  // Lifecycle
  bool begin(uint16_t tcpPort = 9090);
  void stop();
  bool isRunning() const { return running; }

  // Call this periodically to handle clients and discovery
  void loop();

  // Check connection state
  bool isClientConnected() { return client && client.connected(); }

  // Get server info
  uint16_t getTcpPort() const { return tcpPort; }
  uint16_t getUdpPort() const { return udpPort; }

  // Callbacks
  void setStatusCallback(StatusCallback cb) { onStatus = std::move(cb); }
  void setProgressCallback(ProgressCallback cb) { onProgress = std::move(cb); }
  void setBookReceivedCallback(BookReceivedCallback cb) { onBookReceived = std::move(cb); }
  void setBookDeletedCallback(BookDeletedCallback cb) { onBookDeleted = std::move(cb); }

 private:
  WiFiUDP udp;
  WiFiServer server;
  WiFiClient client;

  bool running = false;
  uint16_t tcpPort = 9090;
  uint16_t udpPort = 0;

  // Current transfer state
  bool receiving = false;
  std::string currentBookPath;
  std::string currentBookTitle;
  size_t currentBookSize = 0;
  size_t currentBookReceived = 0;

  // Challenge for password authentication
  std::string currentChallenge;

  // Callbacks
  StatusCallback onStatus;
  ProgressCallback onProgress;
  BookReceivedCallback onBookReceived;
  BookDeletedCallback onBookDeleted;

  // UDP discovery
  bool setupUdpListener();
  void handleUdpDiscovery();

  // TCP protocol handlers
  void handleNewClient();
  void handleClientMessage();

  // Message handlers
  void handleGetInitInfo(const std::string& data);
  void handleNoop();
  void handleTotalSpace();
  void handleFreeSpace();
  void handleGetBookCount();
  void handleSendBooklists(const std::string& data);
  void handleSendBook(const std::string& data);
  void handleDeleteBook(const std::string& data);

  // Book management helpers
  std::vector<CalibreBookInfo> scanBooks();
  std::string buildBooklistJson(const std::vector<CalibreBookInfo>& books);
  std::string sanitizeFilename(const std::string& name);
  bool streamBookToFile(size_t expectedSize, const std::string& destPath);

  // Helpers
  void reportStatus(const char* status);
  void reportProgress(const char* title, size_t received, size_t total);
  std::string generateChallenge();
};
