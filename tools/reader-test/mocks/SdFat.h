#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "Print.h"

// File open mode flags
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02
#define O_CREAT 0x40
#define O_TRUNC 0x80

// FsFile backed by real FILE* I/O (inherits Print for stream compatibility)
class FsFile : public Print {
 public:
  FsFile() = default;
  ~FsFile() { close(); }

  // Move-only (owns FILE*)
  FsFile(FsFile&& other) noexcept : fp_(other.fp_), isOpen_(other.isOpen_), fileSize_(other.fileSize_) {
    other.fp_ = nullptr;
    other.isOpen_ = false;
  }
  FsFile& operator=(FsFile&& other) noexcept {
    if (this != &other) {
      close();
      fp_ = other.fp_;
      isOpen_ = other.isOpen_;
      fileSize_ = other.fileSize_;
      other.fp_ = nullptr;
      other.isOpen_ = false;
    }
    return *this;
  }
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;

  bool open(const char* path, int mode) {
    close();
    const char* fmode;
    if (mode & O_WRONLY) {
      if (mode & O_TRUNC)
        fmode = "wb";
      else
        fmode = "r+b";
    } else if (mode & O_RDWR) {
      if (mode & O_CREAT)
        fmode = "w+b";
      else
        fmode = "r+b";
    } else {
      fmode = "rb";
    }

    fp_ = fopen(path, fmode);
    if (!fp_ && (mode & O_CREAT)) {
      fp_ = fopen(path, "w+b");
    }
    if (!fp_) return false;

    isOpen_ = true;
    // Cache file size
    fseek(fp_, 0, SEEK_END);
    fileSize_ = ftell(fp_);
    fseek(fp_, 0, SEEK_SET);
    return true;
  }

  void close() {
    if (fp_) {
      fclose(fp_);
      fp_ = nullptr;
    }
    isOpen_ = false;
    fileSize_ = 0;
  }

  int read() {
    if (!fp_) return -1;
    int c = fgetc(fp_);
    return c;
  }

  int read(uint8_t* buf, size_t len) {
    if (!fp_) return -1;
    size_t r = fread(buf, 1, len, fp_);
    return static_cast<int>(r);
  }

  // Overload for void* (ZipFile reads into uint32_t*, uint16_t*, etc.)
  int read(void* buf, size_t len) { return read(static_cast<uint8_t*>(buf), len); }

  size_t write(uint8_t c) override {
    if (!fp_) return 0;
    return fwrite(&c, 1, 1, fp_) == 1 ? 1 : 0;
  }

  size_t write(const uint8_t* buf, size_t len) override {
    if (!fp_) return 0;
    size_t w = fwrite(buf, 1, len, fp_);
    // Update cached size if we wrote past end
    size_t pos = ftell(fp_);
    if (pos > fileSize_) fileSize_ = pos;
    return w;
  }

  // Overload matching SdFat's char* signature (used by Fb2 section file generation).
  size_t write(const char* buf, size_t len) {
    return write(reinterpret_cast<const uint8_t*>(buf), len);
  }

  bool seek(size_t pos) {
    if (!fp_) return false;
    return fseek(fp_, static_cast<long>(pos), SEEK_SET) == 0;
  }

  bool seekSet(size_t pos) { return seek(pos); }

  bool seekEnd() {
    if (!fp_) return false;
    return fseek(fp_, 0, SEEK_END) == 0;
  }

  bool seekCur(int offset) {
    if (!fp_) return false;
    return fseek(fp_, offset, SEEK_CUR) == 0;
  }

  size_t position() const {
    if (!fp_) return 0;
    return ftell(fp_);
  }

  size_t size() const { return fileSize_; }

  int available() const {
    if (!fp_) return 0;
    size_t pos = ftell(fp_);
    return static_cast<int>(fileSize_ - pos);
  }

  bool isOpen() const { return isOpen_; }
  operator bool() const { return isOpen_; }

  bool rename(const char* newPath) {
    // Can't rename while open - would need to track path
    (void)newPath;
    return false;
  }

 private:
  FILE* fp_ = nullptr;
  bool isOpen_ = false;
  size_t fileSize_ = 0;
};
