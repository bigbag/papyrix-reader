#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "SdFat.h"

class SDCardManager {
 public:
  SDCardManager() = default;
  bool begin() { return true; }
  bool ready() const { return true; }

  void registerFile(const std::string& path, const std::string& data) { files_[path] = data; }

  // Alias for registerFile - more intuitive name for test setup
  void setFileData(const std::string& path, const std::vector<uint8_t>& data) {
    files_[path] = std::string(data.begin(), data.end());
  }

  void setFileData(const std::string& path, const std::string& data) { files_[path] = data; }

  // Control whether exists() returns true for a path
  void setFileExists(const std::string& path, bool exists) {
    if (exists) {
      // Ensure path is in the map even if no data
      if (files_.find(path) == files_.end()) {
        files_[path] = "";
      }
    } else {
      files_.erase(path);
    }
  }

  void clearFiles() { files_.clear(); }

  // Reset all mock state
  void reset() {
    files_.clear();
    writtenFiles_.clear();
    openFailCount_ = 0;
    openFileForReadFailCount_ = 0;
    mallocFailCount_ = 0;
    readLimit_ = 0;
    readLimitActive_ = false;
  }

  bool exists(const char* path) { return files_.find(path) != files_.end(); }

  bool exists(const std::string& path) { return files_.find(path) != files_.end(); }

  // Failure injection: first N open() calls for a path return an invalid FsFile
  void setOpenFailCount(int count) { openFailCount_ = count; }

  // Failure injection: first N openFileForRead() calls for a path fail
  void setOpenFileForReadFailCount(int count) { openFileForReadFailCount_ = count; }

  // Failure injection: next N malloc calls return nullptr
  void setMallocFailCount(int count) { mallocFailCount_ = count; }
  void setNextMallocFails(bool fails) {
    if (fails && mallocFailCount_ == 0) mallocFailCount_ = 1;
  }

  // Check if malloc should fail (decrements counter)
  bool shouldMallocFail() {
    if (mallocFailCount_ > 0) {
      mallocFailCount_--;
      return true;
    }
    return false;
  }

  // Read limit injection: only allow N bytes to be read
  void setReadLimit(size_t limit) {
    readLimit_ = limit;
    readLimitActive_ = true;
  }

  FsFile open(const char* path, int mode = O_RDONLY) {
    (void)mode;
    FsFile file;
    if (openFailCount_ > 0) {
      openFailCount_--;
      return file;  // Returns invalid FsFile (operator bool = false)
    }
    auto it = files_.find(path);
    if (it != files_.end()) {
      file.setBuffer(it->second);
      if (readLimitActive_) {
        file.setReadLimit(readLimit_);
      }
    }
    return file;
  }

  bool openFileForRead(const char* moduleName, const char* path, FsFile& file) {
    (void)moduleName;
    if (openFileForReadFailCount_ > 0) {
      openFileForReadFailCount_--;
      return false;
    }
    auto it = files_.find(path);
    if (it != files_.end()) {
      file.setBuffer(it->second);
      if (readLimitActive_) {
        file.setReadLimit(readLimit_);
      }
      return true;
    }
    return false;
  }

  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
    return openFileForRead(moduleName, path.c_str(), file);
  }

  bool openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
    (void)moduleName;
    auto buf = std::make_shared<std::string>();
    writtenFiles_[path] = buf;
    file.setSharedBuffer(buf);
    return true;
  }

  // Retrieve buffer written to a file (survives after FsFile destruction via shared_ptr)
  std::string getWrittenData(const std::string& path) const {
    auto it = writtenFiles_.find(path);
    if (it != writtenFiles_.end() && it->second) {
      return *it->second;
    }
    return "";
  }

  static SDCardManager& getInstance() {
    static SDCardManager instance;
    return instance;
  }

  void clearWrittenFiles() { writtenFiles_.clear(); }

 private:
  std::map<std::string, std::string> files_;
  std::map<std::string, std::shared_ptr<std::string>> writtenFiles_;
  int openFailCount_ = 0;
  int openFileForReadFailCount_ = 0;
  int mallocFailCount_ = 0;
  size_t readLimit_ = 0;
  bool readLimitActive_ = false;
};

#define SdMan SDCardManager::getInstance()
