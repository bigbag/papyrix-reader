#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class UpdateClass {
 public:
  static UpdateClass& instance() {
    static UpdateClass inst;
    return inst;
  }

  void reset() {
    beginOk_ = true;
    writeOk_ = true;
    endOk_ = true;
    writeReturnsZero_ = false;
    aborted_ = false;
    begun_ = false;
    totalSize_ = 0;
    totalWritten_ = 0;
    writtenData_.clear();
  }

  bool begin(size_t size) {
    begun_ = true;
    if (!beginOk_) return false;
    totalSize_ = size;
    totalWritten_ = 0;
    writtenData_.clear();
    return true;
  }

  size_t write(const uint8_t* buf, size_t len) {
    if (!writeOk_) return 0;
    if (writeReturnsZero_) return 0;
    writtenData_.insert(writtenData_.end(), buf, buf + len);
    totalWritten_ += len;
    return len;
  }

  bool end(bool /* evenIfRemaining */) {
    if (aborted_) return false;
    return endOk_;
  }

  void abort() { aborted_ = true; }

  // Test hooks
  bool beginOk_ = true;
  bool writeOk_ = true;
  bool endOk_ = true;
  bool writeReturnsZero_ = false;
  bool aborted_ = false;
  bool begun_ = false;
  size_t totalSize_ = 0;
  size_t totalWritten_ = 0;
  std::vector<uint8_t> writtenData_;
};

#define Update UpdateClass::instance()
