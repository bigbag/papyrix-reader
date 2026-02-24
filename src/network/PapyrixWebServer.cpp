#include "PapyrixWebServer.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "../config.h"
#include "html/FilesPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/SleepPageHtml.generated.h"

#define TAG "WEBSERVER"

namespace papyrix {

static void sendGzipHtml(WebServer* server, const char* data, size_t len) {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "text/html", data, len);
}

bool PapyrixWebServer::flushUploadBuffer() {
  if (upload_.bufferPos > 0 && upload_.file) {
    const size_t written = upload_.file.write(upload_.buffer.data(), upload_.bufferPos);
    if (written != upload_.bufferPos) {
      upload_.bufferPos = 0;
      return false;
    }
    upload_.bufferPos = 0;
  }
  return true;
}

PapyrixWebServer::PapyrixWebServer() = default;

PapyrixWebServer::~PapyrixWebServer() { stop(); }

void PapyrixWebServer::begin() {
  if (running_) {
    LOG_DBG(TAG, "Server already running");
    return;
  }

  // Check network connection
  wifi_mode_t wifiMode = WiFi.getMode();
  bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  bool isInApMode = (wifiMode & WIFI_MODE_AP);

  if (!isStaConnected && !isInApMode) {
    LOG_ERR(TAG, "Cannot start - no network connection");
    return;
  }

  apMode_ = isInApMode;

  LOG_INF(TAG, "Creating server on port %d (free heap: %d)", port_, ESP.getFreeHeap());

  server_.reset(new WebServer(port_));
  if (!server_) {
    LOG_ERR(TAG, "Failed to create WebServer");
    return;
  }

  // Setup routes
  server_->on("/", HTTP_GET, [this] { handleRoot(); });
  server_->on("/files", HTTP_GET, [this] { handleFileList(); });
  server_->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server_->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server_->on("/upload", HTTP_POST, [this] { handleUploadPost(); }, [this] { handleUpload(); });
  server_->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });
  server_->on("/delete", HTTP_POST, [this] { handleDelete(); });
  server_->on("/sleep", HTTP_GET, [this] { handleSleepScreens(); });
  server_->on("/api/sleep-screens", HTTP_GET, [this] { handleSleepScreensData(); });
  server_->on("/sleep/delete", HTTP_POST, [this] { handleSleepScreenDelete(); });
  server_->onNotFound([this] { handleNotFound(); });

  server_->begin();
  running_ = true;

  String ipAddr = apMode_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_INF(TAG, "Server started at http://%s/", ipAddr.c_str());
}

void PapyrixWebServer::stop() {
  if (!running_ || !server_) {
    return;
  }

  LOG_INF(TAG, "Stopping server (free heap: %d)", ESP.getFreeHeap());

  running_ = false;
  delay(100);

  server_->stop();
  delay(50);
  server_.reset();

  // Clear upload state
  if (upload_.file) {
    upload_.file.close();
  }
  upload_.fileName = "";
  upload_.path = "/";
  upload_.size = 0;
  upload_.success = false;
  upload_.error = "";
  upload_.bufferPos = 0;
  upload_.buffer.clear();
  upload_.buffer.shrink_to_fit();

  LOG_INF(TAG, "Server stopped (free heap: %d)", ESP.getFreeHeap());
}

void PapyrixWebServer::handleClient() {
  if (!running_ || !server_) {
    return;
  }
  server_->handleClient();
}

void PapyrixWebServer::handleRoot() { sendGzipHtml(server_.get(), HomePageHtml, HomePageHtmlCompressedSize); }

void PapyrixWebServer::handleNotFound() { server_->send(404, "text/plain", "404 Not Found"); }

void PapyrixWebServer::handleStatus() {
  String ipAddr = apMode_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  char json[256];
  snprintf(json, sizeof(json),
           "{\"version\":\"%s\",\"ip\":\"%s\",\"mode\":\"%s\",\"rssi\":%d,\"freeHeap\":%u,\"uptime\":%lu}",
           PAPYRIX_VERSION, ipAddr.c_str(), apMode_ ? "AP" : "STA", apMode_ ? 0 : WiFi.RSSI(), ESP.getFreeHeap(),
           millis() / 1000);

  server_->send(200, "application/json", json);
}

void PapyrixWebServer::handleFileList() { sendGzipHtml(server_.get(), FilesPageHtml, FilesPageHtmlCompressedSize); }

void PapyrixWebServer::handleFileListData() {
  String currentPath = "/";
  if (server_->hasArg("path")) {
    currentPath = server_->arg("path");
    if (!currentPath.startsWith("/")) {
      currentPath = "/" + currentPath;
    }
    if (currentPath.length() > 1 && currentPath.endsWith("/")) {
      currentPath = currentPath.substring(0, currentPath.length() - 1);
    }
  }

  FsFile root = SdMan.open(currentPath.c_str());
  if (!root || !root.isDirectory()) {
    server_->send(404, "application/json", "[]");
    if (root) root.close();
    return;
  }

  server_->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_->send(200, "application/json", "");
  server_->sendContent("[");

  char name[256];
  bool seenFirst = false;
  FsFile file = root.openNextFile();

  while (file) {
    file.getName(name, sizeof(name));

    // Skip hidden items
    if (name[0] != '.' && !FsHelpers::isHiddenFsItem(name)) {
      JsonDocument doc;
      doc["name"] = name;
      doc["isDirectory"] = file.isDirectory();

      if (file.isDirectory()) {
        doc["size"] = 0;
        doc["isEpub"] = false;
      } else {
        doc["size"] = file.size();
        doc["isEpub"] = FsHelpers::isEpubFile(name);
      }

      char output[512];
      size_t written = serializeJson(doc, output, sizeof(output));
      if (written < sizeof(output)) {
        if (seenFirst) {
          server_->sendContent(",");
        } else {
          seenFirst = true;
        }
        server_->sendContent(output);
      }
    }

    file.close();
    file = root.openNextFile();
  }

  root.close();
  server_->sendContent("]");
  server_->sendContent("");
}

void PapyrixWebServer::handleUpload() {
  if (!running_ || !server_) return;

  HTTPUpload& upload = server_->upload();

  if (upload.status == UPLOAD_FILE_START) {
    upload_.fileName = upload.filename;
    upload_.size = 0;
    upload_.success = false;
    upload_.error = "";
    upload_.bufferPos = 0;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < UploadState::BUFFER_SIZE * 2) {
      upload_.error = "Insufficient memory for upload";
      return;
    }
    upload_.buffer.resize(UploadState::BUFFER_SIZE);

    if (server_->hasArg("path")) {
      upload_.path = server_->arg("path");
      if (!upload_.path.startsWith("/")) {
        upload_.path = "/" + upload_.path;
      }
      if (upload_.path.length() > 1 && upload_.path.endsWith("/")) {
        upload_.path = upload_.path.substring(0, upload_.path.length() - 1);
      }
    } else {
      upload_.path = "/";
    }

    LOG_INF(TAG, "Upload start: %s to %s", upload_.fileName.c_str(), upload_.path.c_str());

    String filePath = upload_.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += upload_.fileName;

    if (!FsHelpers::isSupportedBookFile(upload_.fileName.c_str()) &&
        !FsHelpers::isImageFile(upload_.fileName.c_str())) {
      upload_.error = "Unsupported file type";
      LOG_ERR(TAG, "Rejected upload: %s (unsupported type)", upload_.fileName.c_str());
      return;
    }

    if (SdMan.exists(filePath.c_str())) {
      SdMan.remove(filePath.c_str());
    }

    if (!SdMan.openFileForWrite("WEB", filePath, upload_.file)) {
      upload_.error = "Failed to create file";
      LOG_ERR(TAG, "Failed to create: %s", filePath.c_str());
      return;
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upload_.file && upload_.error.isEmpty()) {
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        size_t space = UploadState::BUFFER_SIZE - upload_.bufferPos;
        size_t toCopy = remaining < space ? remaining : space;
        memcpy(upload_.buffer.data() + upload_.bufferPos, data, toCopy);
        upload_.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        if (upload_.bufferPos >= UploadState::BUFFER_SIZE) {
          if (!flushUploadBuffer()) {
            upload_.error = "Write failed - disk full?";
            upload_.file.close();
            return;
          }
        }
      }

      upload_.size += upload.currentSize;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (upload_.file) {
      if (upload_.error.isEmpty() && !flushUploadBuffer()) {
        upload_.error = "Write failed - disk full?";
      }
      upload_.file.close();
      if (upload_.error.isEmpty()) {
        upload_.success = true;
        LOG_INF(TAG, "Upload complete: %s (%zu bytes)", upload_.fileName.c_str(), upload_.size);
      }
    }
    upload_.buffer.clear();
    upload_.buffer.shrink_to_fit();

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    upload_.bufferPos = 0;
    upload_.buffer.clear();
    upload_.buffer.shrink_to_fit();
    if (upload_.file) {
      upload_.file.close();
      String filePath = upload_.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += upload_.fileName;
      SdMan.remove(filePath.c_str());
    }
    upload_.error = "Upload aborted";
    LOG_ERR(TAG, "Upload aborted");
  }
}

void PapyrixWebServer::handleUploadPost() {
  if (upload_.success) {
    server_->send(200, "text/plain", "File uploaded: " + upload_.fileName);
  } else {
    String error = upload_.error.isEmpty() ? "Unknown error" : upload_.error;
    server_->send(400, "text/plain", error);
  }
}

void PapyrixWebServer::handleCreateFolder() {
  if (!server_->hasArg("name")) {
    server_->send(400, "text/plain", "Missing folder name");
    return;
  }

  String folderName = server_->arg("name");
  if (folderName.isEmpty()) {
    server_->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }

  String parentPath = "/";
  if (server_->hasArg("path")) {
    parentPath = server_->arg("path");
    if (!parentPath.startsWith("/")) {
      parentPath = "/" + parentPath;
    }
    if (parentPath.length() > 1 && parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length() - 1);
    }
  }

  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  if (SdMan.exists(folderPath.c_str())) {
    server_->send(400, "text/plain", "Folder already exists");
    return;
  }

  if (SdMan.mkdir(folderPath.c_str())) {
    LOG_INF(TAG, "Created folder: %s", folderPath.c_str());
    server_->send(200, "text/plain", "Folder created");
  } else {
    server_->send(500, "text/plain", "Failed to create folder");
  }
}

void PapyrixWebServer::handleDelete() {
  if (!server_->hasArg("path")) {
    server_->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server_->arg("path");
  String itemType = server_->hasArg("type") ? server_->arg("type") : "file";

  if (itemPath.isEmpty() || itemPath == "/") {
    server_->send(400, "text/plain", "Cannot delete root");
    return;
  }

  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  // Security: prevent deletion of hidden/system files
  String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".") || FsHelpers::isHiddenFsItem(itemName.c_str())) {
    server_->send(403, "text/plain", "Cannot delete system files");
    return;
  }

  if (!SdMan.exists(itemPath.c_str())) {
    server_->send(404, "text/plain", "Item not found");
    return;
  }

  bool success = false;
  if (itemType == "folder") {
    FsFile dir = SdMan.open(itemPath.c_str());
    if (dir && dir.isDirectory()) {
      FsFile entry = dir.openNextFile();
      if (entry) {
        entry.close();
        dir.close();
        server_->send(400, "text/plain", "Folder not empty");
        return;
      }
      dir.close();
    }
    success = SdMan.rmdir(itemPath.c_str());
  } else {
    success = SdMan.remove(itemPath.c_str());
  }

  if (success) {
    LOG_INF(TAG, "Deleted: %s", itemPath.c_str());
    server_->send(200, "text/plain", "Deleted");
  } else {
    server_->send(500, "text/plain", "Failed to delete");
  }
}

void PapyrixWebServer::handleSleepScreens() { sendGzipHtml(server_.get(), SleepPageHtml, SleepPageHtmlCompressedSize); }

void PapyrixWebServer::handleSleepScreensData() {
  FsFile root = SdMan.open("/sleep");
  if (!root || !root.isDirectory()) {
    server_->send(200, "application/json", "[]");
    if (root) root.close();
    return;
  }

  server_->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_->send(200, "application/json", "");
  server_->sendContent("[");

  char name[256];
  bool seenFirst = false;
  FsFile file = root.openNextFile();

  while (file) {
    file.getName(name, sizeof(name));

    if (name[0] != '.' && !file.isDirectory()) {
      // Only include .bmp files
      const char* dot = strrchr(name, '.');
      if (dot && (strcasecmp(dot, ".bmp") == 0)) {
        JsonDocument doc;
        doc["name"] = name;
        doc["size"] = file.size();

        char output[512];
        size_t written = serializeJson(doc, output, sizeof(output));
        if (written < sizeof(output)) {
          if (seenFirst) {
            server_->sendContent(",");
          } else {
            seenFirst = true;
          }
          server_->sendContent(output);
        }
      }
    }

    file.close();
    file = root.openNextFile();
  }

  root.close();
  server_->sendContent("]");
  server_->sendContent("");
}

void PapyrixWebServer::handleSleepScreenDelete() {
  if (!server_->hasArg("name")) {
    server_->send(400, "text/plain", "Missing file name");
    return;
  }

  String name = server_->arg("name");
  if (name.isEmpty()) {
    server_->send(400, "text/plain", "File name cannot be empty");
    return;
  }

  // Security: reject path traversal
  if (name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
    server_->send(400, "text/plain", "Invalid file name");
    return;
  }

  // Only allow .bmp files
  String lower = name;
  lower.toLowerCase();
  if (!lower.endsWith(".bmp")) {
    server_->send(400, "text/plain", "Only BMP files can be deleted");
    return;
  }

  String filePath = "/sleep/" + name;

  if (!SdMan.exists(filePath.c_str())) {
    server_->send(404, "text/plain", "File not found");
    return;
  }

  if (SdMan.remove(filePath.c_str())) {
    LOG_INF(TAG, "Deleted sleep screen: %s", filePath.c_str());
    server_->send(200, "text/plain", "Deleted");
  } else {
    server_->send(500, "text/plain", "Failed to delete");
  }
}

}  // namespace papyrix
