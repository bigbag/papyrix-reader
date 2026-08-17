#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Print.h"

// File open mode flags
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02
#define O_CREAT 0x40
#define O_TRUNC 0x80

struct MockDirectoryEntry {
  std::string name;
  bool isDirectory;
};

// Mock FsFile for testing serialization
class FsFile : public Print {
 public:
  FsFile() = default;

  // For testing with in-memory buffer
  void setBuffer(const std::string& data) {
    buffer_ = data;
    sharedBuffer_.reset();
    directoryEntries_.clear();
    directoryIndex_ = 0;
    name_.clear();
    pos_ = 0;
    isOpen_ = true;
    isDirectory_ = false;
    totalRead_ = 0;
    readLimitActive_ = false;
    totalWritten_ = 0;
    writeLimitActive_ = false;
    syncResult_ = true;
    seekEndResult_ = true;
    hasModifyDateTime_ = false;
  }

  // For write-mode: use a shared buffer so data survives after FsFile destruction
  void setSharedBuffer(std::shared_ptr<std::string> buf) {
    sharedBuffer_ = buf;
    buffer_ = *buf;
    directoryEntries_.clear();
    directoryIndex_ = 0;
    name_.clear();
    pos_ = 0;
    isOpen_ = true;
    isDirectory_ = false;
    totalRead_ = 0;
    readLimitActive_ = false;
    totalWritten_ = 0;
    writeLimitActive_ = false;
    syncResult_ = true;
    seekEndResult_ = true;
    hasModifyDateTime_ = false;
  }

  void setDirectory(const std::vector<MockDirectoryEntry>& entries) {
    buffer_.clear();
    sharedBuffer_.reset();
    directoryEntries_ = entries;
    directoryIndex_ = 0;
    name_.clear();
    pos_ = 0;
    isOpen_ = true;
    isDirectory_ = true;
  }

  void setDirectoryEntry(const MockDirectoryEntry& entry) {
    buffer_.clear();
    sharedBuffer_.reset();
    directoryEntries_.clear();
    directoryIndex_ = 0;
    name_ = entry.name;
    pos_ = 0;
    isOpen_ = true;
    isDirectory_ = entry.isDirectory;
  }

  // Read limit injection: only allow N bytes to be read
  void setReadLimit(size_t limit) {
    readLimit_ = limit;
    readLimitActive_ = true;
  }

  void setWriteLimit(size_t limit) {
    writeLimit_ = limit;
    writeLimitActive_ = true;
  }

  void setSyncResult(bool result) { syncResult_ = result; }
  void setSeekEndResult(bool result) { seekEndResult_ = result; }

  void setModifyDateTime(uint16_t date, uint16_t time) {
    modifyDate_ = date;
    modifyTime_ = time;
    hasModifyDateTime_ = true;
  }

  bool getModifyDateTime(uint16_t* date, uint16_t* time) const {
    if (!hasModifyDateTime_ || !date || !time) return false;
    *date = modifyDate_;
    *time = modifyTime_;
    return true;
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
    sharedBuffer_.reset();
    directoryEntries_.clear();
    directoryIndex_ = 0;
    name_.clear();
    isOpen_ = false;
    isDirectory_ = false;
    pos_ = 0;
    readLimit_ = 0;
    readLimitActive_ = false;
    totalRead_ = 0;
    writeLimit_ = 0;
    writeLimitActive_ = false;
    totalWritten_ = 0;
    syncResult_ = true;
    seekEndResult_ = true;
    modifyDate_ = 0;
    modifyTime_ = 0;
    hasModifyDateTime_ = false;
  }

  FsFile openNextFile() {
    FsFile entry;
    if (!isOpen_ || !isDirectory_ || directoryIndex_ >= directoryEntries_.size()) return entry;
    entry.setDirectoryEntry(directoryEntries_[directoryIndex_++]);
    return entry;
  }

  void getName(char* out, size_t size) const {
    if (!out || size == 0) return;
    const size_t length = std::min(name_.size(), size - 1);
    memcpy(out, name_.data(), length);
    out[length] = '\0';
  }

  bool isDirectory() const { return isDirectory_; }
  bool isOpen() const { return isOpen_; }
  uint32_t fileSize() const { return isOpen_ ? static_cast<uint32_t>(buffer_.size()) : 0; }
  uint64_t fileSize64() const { return isOpen_ ? buffer_.size() : 0; }
  size_t size() const { return isOpen_ ? buffer_.size() : 0; }

  size_t position() const { return pos_; }

  bool seek(size_t pos) {
    if (pos > buffer_.size()) return false;
    pos_ = pos;
    return true;
  }

  bool seekSet(size_t pos) { return seek(pos); }
  bool seekEnd(int64_t offset = 0) {
    if (!seekEndResult_) return false;
    const int64_t position = static_cast<int64_t>(buffer_.size()) + offset;
    return position >= 0 && seek(static_cast<size_t>(position));
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

  // Read into uint16_t* (len is byte count, matching FsFile byte-oriented read)
  int read(uint16_t* buf, size_t len) { return read(reinterpret_cast<uint8_t*>(buf), len); }

  // Read into uint32_t* (len is byte count, matching FsFile byte-oriented read)
  int read(uint32_t* buf, size_t len) { return read(reinterpret_cast<uint8_t*>(buf), len); }

  size_t write(uint8_t byte) {
    if (!isOpen_ || (writeLimitActive_ && totalWritten_ >= writeLimit_)) return 0;
    if (pos_ >= buffer_.size()) {
      buffer_.resize(pos_ + 1);
    }
    buffer_[pos_++] = static_cast<char>(byte);
    totalWritten_++;
    return 1;
  }

  // Arduino Print compatibility: char-pointer overload delegating to the
  // byte-oriented write (real HardwareSDK Print provides this).
  size_t write(const char* str, size_t len) { return write(reinterpret_cast<const uint8_t*>(str), len); }

  size_t write(const uint8_t* buf, size_t len) {
    if (!isOpen_) return 0;
    if (writeLimitActive_) {
      const size_t remaining = writeLimit_ - totalWritten_;
      len = std::min(len, remaining);
    }
    if (len == 0) return 0;
    // Extend buffer if needed
    if (pos_ + len > buffer_.size()) {
      buffer_.resize(pos_ + len);
    }
    memcpy(&buffer_[pos_], buf, len);
    pos_ += len;
    totalWritten_ += len;
    return len;
  }

  bool sync() {
    if (!syncResult_) return false;
    if (sharedBuffer_) *sharedBuffer_ = buffer_;
    return true;
  }

  bool available() const { return isOpen_ && pos_ < buffer_.size(); }

 private:
  std::string buffer_;
  std::shared_ptr<std::string> sharedBuffer_;
  std::vector<MockDirectoryEntry> directoryEntries_;
  std::string name_;
  size_t directoryIndex_ = 0;
  size_t pos_ = 0;
  bool isOpen_ = false;
  bool isDirectory_ = false;
  size_t readLimit_ = 0;
  bool readLimitActive_ = false;
  size_t totalRead_ = 0;
  size_t writeLimit_ = 0;
  bool writeLimitActive_ = false;
  size_t totalWritten_ = 0;
  bool syncResult_ = true;
  bool seekEndResult_ = true;
  uint16_t modifyDate_ = 0;
  uint16_t modifyTime_ = 0;
  bool hasModifyDateTime_ = false;
};
