#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// File open mode flags
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02
#define O_CREAT 0x40
#define O_TRUNC 0x80

// Mock FsFile for testing serialization
class FsFile {
 public:
  FsFile() = default;

  // For testing with in-memory buffer
  void setBuffer(const std::string& data) {
    buffer_ = data;
    pos_ = 0;
    isOpen_ = true;
  }

  std::string getBuffer() const { return buffer_; }

  operator bool() const { return isOpen_; }

  bool open(const char* path, int mode) {
    (void)path;
    (void)mode;
    isOpen_ = true;
    return true;
  }

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

  int read() {
    if (!isOpen_ || pos_ >= buffer_.size()) return -1;
    return static_cast<unsigned char>(buffer_[pos_++]);
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
    // Extend buffer if needed
    if (pos_ + len > buffer_.size()) {
      buffer_.resize(pos_ + len);
    }
    memcpy(&buffer_[pos_], buf, len);
    pos_ += len;
    return len;
  }

  bool available() const { return isOpen_ && pos_ < buffer_.size(); }

 private:
  std::string buffer_;
  size_t pos_ = 0;
  bool isOpen_ = false;
};
