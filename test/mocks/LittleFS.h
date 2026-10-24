#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

// Mock File class for LittleFS
class File {
 public:
  File() = default;

  void setBuffer(const std::string& data) {
    buffer_ = data;
    pos_ = 0;
    isOpen_ = true;
  }

  operator bool() const { return isOpen_; }

  void close() {
    isOpen_ = false;
    pos_ = 0;
  }

  size_t size() const { return buffer_.size(); }

  size_t position() const { return pos_; }

  bool seek(size_t pos) {
    if (pos > buffer_.size()) return false;
    pos_ = pos;
    return true;
  }

  int read(uint8_t* buf, size_t len) {
    if (!isOpen_) return -1;
    size_t toRead = std::min(len, buffer_.size() - pos_);
    if (toRead == 0) return 0;
    memcpy(buf, buffer_.data() + pos_, toRead);
    pos_ += toRead;
    return static_cast<int>(toRead);
  }

  size_t write(const uint8_t* buf, size_t len) {
    if (!isOpen_) return 0;
    if (pos_ + len > buffer_.size()) {
      buffer_.resize(pos_ + len);
    }
    memcpy(&buffer_[pos_], buf, len);
    pos_ += len;
    return len;
  }

 private:
  std::string buffer_;
  size_t pos_ = 0;
  bool isOpen_ = false;
};

// Mock LittleFS filesystem
class MockLittleFS {
 public:
  void registerFile(const std::string& path, const std::string& data) { files_[path] = data; }

  void clearFiles() { files_.clear(); }
  size_t formatCalls = 0;
  bool format() { ++formatCalls; clearFiles(); return true; }
  size_t totalBytes() const { return 1024 * 1024; }
  size_t usedBytes() const {
    size_t bytes = 0;
    for (const auto& file : files_) bytes += file.second.size();
    return bytes;
  }

  File open(const char* path, const char* mode = "r") {
    (void)mode;
    File file;
    auto it = files_.find(path);
    if (it != files_.end()) {
      file.setBuffer(it->second);
    }
    return file;
  }

  bool exists(const char* path) { return files_.find(path) != files_.end(); }

 private:
  std::map<std::string, std::string> files_;
};

extern MockLittleFS LittleFS;
