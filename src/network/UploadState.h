#pragma once

#include <SDCardManager.h>
#include <WString.h>

#include <cstdint>
#include <cstdio>

#include "../config.h"

namespace papyrix {

struct UploadState {
  String fileName;
  String path = "/";
  size_t size = 0;
  bool success = false;
  String error;

  UploadState() = default;
  UploadState(const UploadState&) = delete;
  UploadState& operator=(const UploadState&) = delete;
  ~UploadState() { abort(); }

  void reset() {
    abort();
    fileName = "";
    path = "/";
    size = 0;
    success = false;
    error = "";
  }

  bool begin(const String& destination) {
    abort();
    size = 0;
    success = false;
    error = "";
    destination_ = destination;
    if (SdMan.exists(destination_.c_str())) {
      FsFile existing = SdMan.open(destination_.c_str());
      if (!existing || existing.isDirectory()) {
        error = "Cannot replace destination";
        return false;
      }
      existing.close();
    }
    if (!SdMan.ensureDirectoryExists(PAPYRIX_DIR)) {
      error = "Failed to create upload directory";
      return false;
    }
    static uint32_t nextId = 0;
    // ponytail: Try 16 names. Add orphan recovery if repeated power cuts exhaust them.
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      const auto id = static_cast<unsigned long>(nextId++);
      snprintf(temporary_, sizeof(temporary_), PAPYRIX_DIR "/.upload-%08lx.tmp", id);
      snprintf(backup_, sizeof(backup_), PAPYRIX_DIR "/.upload-%08lx.bak", id);
      if (SdMan.exists(temporary_) || SdMan.exists(backup_)) continue;
      file_ = SdMan.open(temporary_, O_WRONLY | O_CREAT | O_EXCL);
      if (file_) return true;
      break;
    }
    temporary_[0] = '\0';
    error = "Failed to create upload file";
    return false;
  }

  void write(const uint8_t* data, size_t length) {
    if (!file_ || !error.isEmpty()) return;
    if (file_.write(data, length) != length) {
      error = "Write failed - disk full?";
      abort();
      return;
    }
    size += length;
  }

  void finish() {
    if (!file_ || !error.isEmpty()) return;
    if (!file_.sync()) {
      error = "Write failed - disk full?";
      abort();
      return;
    }
    file_.close();
    const bool hadFile = SdMan.exists(destination_.c_str());
    if (hadFile && !SdMan.rename(destination_.c_str(), backup_)) {
      error = "Failed to preserve original file";
      // FatFile::rename removes the old entry before its final cacheSync, so
      // a false return can mean the move already happened.
      if (!SdMan.exists(destination_.c_str()) && SdMan.exists(backup_)) {
        if (SdMan.rename(backup_, destination_.c_str()) || SdMan.exists(destination_.c_str())) {
          error = "Upload failed - original restored";
        } else {
          error = String("Original file retained at ") + backup_;
        }
      }
      abort();
      return;
    }
    if (!SdMan.commitFile(temporary_, destination_.c_str())) {
      error = "Failed to replace file";
      // Only promise a backup that still exists; a false rollback may have
      // moved the original back despite reporting failure.
      if (hadFile && !SdMan.rename(backup_, destination_.c_str()) && SdMan.exists(backup_)) {
        error = String("Original file retained at ") + backup_;
      }
      abort();
      return;
    }
    temporary_[0] = '\0';
    if (hadFile) SdMan.remove(backup_);
    success = true;
  }

  void abort() {
    if (file_) file_.close();
    if (temporary_[0] != '\0') {
      SdMan.remove(temporary_);
      temporary_[0] = '\0';
    }
  }

 private:
  FsFile file_;
  String destination_;
  char temporary_[sizeof(PAPYRIX_DIR) + 32] = {};
  char backup_[sizeof(PAPYRIX_DIR) + 32] = {};
};

}  // namespace papyrix
