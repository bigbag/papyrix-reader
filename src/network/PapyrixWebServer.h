#pragma once

#include <SDCardManager.h>
#include <WebServer.h>

#include <memory>

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
    bool ownsFile = false;
    bool success = false;
    String error = "";
  };

  std::unique_ptr<WebServer> server_;
  bool running_ = false;
  bool apMode_ = false;
  uint16_t port_ = 80;
  UploadState upload_;

  // Request handlers
  void handleRoot();
  void handleNotFound();
  void handleStatus();
  void handleFileListData();
  void handleUpload();
  void handleUploadPost();
  void handleCreateFolder();
  void handleDelete();
  void handleDownload();
  void handleRename();

  // Locale file management
  void handleLocaleStatus();
  void handleLocaleUpload();
  void handleLocaleUploadPost();
  void handleLocaleDelete();

  // Firmware file management
  void handleFirmwareStatus();
  void handleFirmwareUpload();
  void handleFirmwareUploadPost();
  void handleFirmwareDelete();
};

}  // namespace papyrix
