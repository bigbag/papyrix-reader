#pragma once

#include <map>
#include <memory>
#include <string>

#include "SdFat.h"

class SDCardManager {
 public:
  SDCardManager() = default;
  bool begin() { return true; }
  bool ready() const { return true; }

  void registerFile(const std::string& path, const std::string& data) { files_[path] = data; }

  void clearFiles() { files_.clear(); }

  bool exists(const char* path) { return files_.find(path) != files_.end(); }

  // Failure injection: first N open() calls for a path return an invalid FsFile
  void setOpenFailCount(int count) { openFailCount_ = count; }

  // Failure injection: first N openFileForRead() calls for a path fail
  void setOpenFileForReadFailCount(int count) { openFileForReadFailCount_ = count; }

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
};

#define SdMan SDCardManager::getInstance()
