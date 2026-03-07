#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

// Forward declare Print (defined in platform_stubs.h)
class Print;

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
    sharedBuffer_.reset();
    pos_ = 0;
    isOpen_ = true;
  }

  // For write-mode: use a shared buffer so data survives after FsFile destruction
  void setSharedBuffer(std::shared_ptr<std::string> buf) {
    sharedBuffer_ = buf;
    buffer_ = *buf;
    pos_ = 0;
    isOpen_ = true;
  }

  // Read limit injection: only allow N bytes to be read
  void setReadLimit(size_t limit) {
    readLimit_ = limit;
    readLimitActive_ = true;
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
    if (sharedBuffer_) {
      *sharedBuffer_ = buffer_;
    }
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

  bool seekCur(int offset) {
    const auto newPos = static_cast<int64_t>(pos_) + offset;
    if (newPos < 0 || static_cast<size_t>(newPos) > buffer_.size()) return false;
    pos_ = static_cast<size_t>(newPos);
    return true;
  }

  int read() {
    if (!isOpen_ || pos_ >= buffer_.size()) return -1;
    if (readLimitActive_ && totalRead_ >= readLimit_) return -1;
    totalRead_++;
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

  int read(uint8_t* buf, size_t len) {
    if (!isOpen_) return -1;
    if (readLimitActive_) {
      size_t remaining = readLimit_ - totalRead_;
      if (remaining == 0) return 0;
      len = std::min(len, remaining);
    }
    size_t toRead = std::min(len, buffer_.size() - pos_);
    if (toRead == 0) return 0;
    memcpy(buf, buffer_.data() + pos_, toRead);
    pos_ += toRead;
    totalRead_ += toRead;
    return static_cast<int>(toRead);
  }

  // Overload for char* (common in C code)
  int read(char* buf, size_t len) { return read(reinterpret_cast<uint8_t*>(buf), len); }

  // Overload for void* (used by some libraries)
  int read(void* buf, size_t len) { return read(static_cast<uint8_t*>(buf), len); }

  // Read into uint16_t* (used for reading 16-bit values)
  int read(uint16_t* buf, size_t len) {
    int bytes = read(reinterpret_cast<uint8_t*>(buf), len * 2);
    return bytes > 0 ? bytes / 2 : bytes;
  }

  // Read into uint32_t* (used for reading 32-bit values)
  int read(uint32_t* buf, size_t len) {
    int bytes = read(reinterpret_cast<uint8_t*>(buf), len * 4);
    return bytes > 0 ? bytes / 4 : bytes;
  }

  size_t write(uint8_t byte) {
    if (!isOpen_) return 0;
    if (pos_ >= buffer_.size()) {
      buffer_.resize(pos_ + 1);
    }
    buffer_[pos_++] = static_cast<char>(byte);
    return 1;
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
  std::shared_ptr<std::string> sharedBuffer_;
  size_t pos_ = 0;
  bool isOpen_ = false;
  size_t readLimit_ = 0;
  bool readLimitActive_ = false;
  size_t totalRead_ = 0;
};
