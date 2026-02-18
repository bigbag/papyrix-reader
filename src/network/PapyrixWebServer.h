#pragma once

#include <SDCardManager.h>
#include <WebServer.h>

#include <memory>
#include <vector>

namespace papyrix {

class PapyrixWebServer {
 public:
  PapyrixWebServer();
  ~PapyrixWebServer();

  void begin();
  void stop();
  void handleClient();

  bool isRunning() const { return running_; }
  uint16_t getPort() const { return port_; }

 private:
  struct UploadState {
    FsFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;
  };

  bool flushUploadBuffer();

  std::unique_ptr<WebServer> server_;
  bool running_ = false;
  bool apMode_ = false;
  uint16_t port_ = 80;
  UploadState upload_;

  // Request handlers
  void handleRoot();
  void handleNotFound();
  void handleStatus();
  void handleFileList();
  void handleFileListData();
  void handleUpload();
  void handleUploadPost();
  void handleCreateFolder();
  void handleDelete();
  void handleSleepScreens();
  void handleSleepScreensData();
  void handleSleepScreenDelete();
};

}  // namespace papyrix
