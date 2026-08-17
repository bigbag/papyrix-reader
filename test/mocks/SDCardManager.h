#pragma once

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "SdFat.h"

class SDCardManager {
 public:
  SDCardManager() = default;
  bool begin() { return true; }
  bool ready() const { return true; }

  void registerFile(const std::string& path, const std::string& data) {
    files_[path] = data;
    writtenFiles_.erase(path);
    directories_.erase(path);
  }

  void registerDirectory(const std::string& path, const std::vector<MockDirectoryEntry>& entries) {
    directories_[path] = entries;
    files_.erase(path);
    writtenFiles_.erase(path);
    modifyDateTimes_.erase(path);
  }

  // Alias for registerFile - more intuitive name for test setup
  void setFileData(const std::string& path, const std::vector<uint8_t>& data) {
    registerFile(path, std::string(data.begin(), data.end()));
  }

  void setFileData(const std::string& path, const std::string& data) { registerFile(path, data); }

  void setFileModifyDateTime(const std::string& path, uint16_t date, uint16_t time) {
    modifyDateTimes_[path] = {date, time};
  }

  // Control whether exists() returns true for a path
  void setFileExists(const std::string& path, bool exists) {
    if (exists) {
      // Ensure path is in the map even if no data
      if (files_.find(path) == files_.end()) {
        files_[path] = "";
      }
    } else {
      files_.erase(path);
      modifyDateTimes_.erase(path);
    }
  }

  void clearFiles() {
    files_.clear();
    writtenFiles_.clear();
    directories_.clear();
    modifyDateTimes_.clear();
  }

  // Reset all mock state
  void reset() {
    files_.clear();
    writtenFiles_.clear();
    directories_.clear();
    modifyDateTimes_.clear();
    openFailCount_ = 0;
    openFileForReadFailCount_ = 0;
    mallocFailCount_ = 0;
    readLimit_ = 0;
    readLimitActive_ = false;
    writeLimit_ = 0;
    writeLimitActive_ = false;
    syncResult_ = true;
    seekEndResult_ = true;
    renameResult_ = true;
  }

  bool exists(const char* path) {
    return files_.find(path) != files_.end() || writtenFiles_.find(path) != writtenFiles_.end() ||
           directories_.find(path) != directories_.end();
  }

  bool exists(const std::string& path) { return exists(path.c_str()); }

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

  void setWriteLimit(size_t limit) {
    writeLimit_ = limit;
    writeLimitActive_ = true;
  }

  void setSyncResult(bool result) { syncResult_ = result; }
  void setSeekEndResult(bool result) { seekEndResult_ = result; }
  void setRenameResult(bool result) { renameResult_ = result; }

  FsFile open(const char* path, int mode = O_RDONLY) {
    FsFile file;
    if (openFailCount_ > 0) {
      openFailCount_--;
      return file;
    }

    const bool writable = (mode & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) != 0;
    if (writable) {
      std::shared_ptr<std::string> buffer;
      auto written = writtenFiles_.find(path);
      if (written != writtenFiles_.end()) {
        buffer = written->second;
      } else {
        buffer = std::make_shared<std::string>();
        auto existing = files_.find(path);
        if (existing != files_.end()) *buffer = existing->second;
        writtenFiles_[path] = buffer;
      }
      if ((mode & O_TRUNC) != 0) buffer->clear();
      file.setSharedBuffer(buffer);
      auto timestamp = modifyDateTimes_.find(path);
      if (timestamp != modifyDateTimes_.end()) {
        file.setModifyDateTime(timestamp->second.first, timestamp->second.second);
      }
      if (writeLimitActive_) file.setWriteLimit(writeLimit_);
      file.setSyncResult(syncResult_);
      file.setSeekEndResult(seekEndResult_);
      return file;
    }

    auto directory = directories_.find(path);
    if (directory != directories_.end()) {
      file.setDirectory(directory->second);
      return file;
    }

    auto existing = files_.find(path);
    if (existing != files_.end()) {
      file.setBuffer(existing->second);
    } else {
      auto written = writtenFiles_.find(path);
      if (written != writtenFiles_.end()) file.setBuffer(*written->second);
    }
    if (file) {
      auto timestamp = modifyDateTimes_.find(path);
      if (timestamp != modifyDateTimes_.end()) {
        file.setModifyDateTime(timestamp->second.first, timestamp->second.second);
      }
      if (readLimitActive_) file.setReadLimit(readLimit_);
    }
    return file;
  }

  bool openFileForRead(const char* moduleName, const char* path, FsFile& file) {
    (void)moduleName;
    if (openFileForReadFailCount_ > 0) {
      openFileForReadFailCount_--;
      return false;
    }
    file = open(path, O_RDONLY);
    return static_cast<bool>(file);
  }

  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
    return openFileForRead(moduleName, path.c_str(), file);
  }

  bool openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
    (void)moduleName;
    auto buf = std::make_shared<std::string>();
    writtenFiles_[path] = buf;
    file.setSharedBuffer(buf);
    auto timestamp = modifyDateTimes_.find(path);
    if (timestamp != modifyDateTimes_.end()) {
      file.setModifyDateTime(timestamp->second.first, timestamp->second.second);
    }
    if (writeLimitActive_) file.setWriteLimit(writeLimit_);
    file.setSyncResult(syncResult_);
    file.setSeekEndResult(seekEndResult_);
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

  bool remove(const char* path) {
    files_.erase(path);
    writtenFiles_.erase(path);
    directories_.erase(path);
    modifyDateTimes_.erase(path);
    return true;
  }

  bool rename(const char* oldPath, const char* newPath) {
    if (!renameResult_) return false;
    if (std::strcmp(oldPath, newPath) == 0) return exists(oldPath);
    auto it = files_.find(oldPath);
    if (it != files_.end()) {
      files_[newPath] = it->second;
      files_.erase(it);
    }
    auto wit = writtenFiles_.find(oldPath);
    if (wit != writtenFiles_.end()) {
      writtenFiles_[newPath] = wit->second;
      writtenFiles_.erase(wit);
    }
    auto directory = directories_.find(oldPath);
    if (directory != directories_.end()) {
      directories_[newPath] = std::move(directory->second);
      directories_.erase(directory);
    }
    auto timestamp = modifyDateTimes_.find(oldPath);
    if (timestamp != modifyDateTimes_.end()) {
      modifyDateTimes_[newPath] = timestamp->second;
      modifyDateTimes_.erase(timestamp);
    }
    return true;
  }

  bool commitFile(const char* tmpPath, const char* finalPath) {
    remove(finalPath);
    return rename(tmpPath, finalPath);
  }

  bool mkdir(const char* path) {
    directories_[path] = {};
    return true;
  }
  bool ensureDirectoryExists(const char* path) { return exists(path) || mkdir(path); }
  bool removeDir(const char* path) {
    directories_.erase(path);
    modifyDateTimes_.erase(path);
    return true;
  }

  static SDCardManager& getInstance() {
    static SDCardManager instance;
    return instance;
  }

  void clearWrittenFiles() { writtenFiles_.clear(); }

  std::vector<std::string> writtenFilePaths() const {
    std::vector<std::string> paths;
    paths.reserve(writtenFiles_.size());
    for (const auto& item : writtenFiles_) paths.push_back(item.first);
    return paths;
  }

 private:
  std::map<std::string, std::string> files_;
  std::map<std::string, std::shared_ptr<std::string>> writtenFiles_;
  std::map<std::string, std::vector<MockDirectoryEntry>> directories_;
  std::map<std::string, std::pair<uint16_t, uint16_t>> modifyDateTimes_;
  int openFailCount_ = 0;
  int openFileForReadFailCount_ = 0;
  int mallocFailCount_ = 0;
  size_t readLimit_ = 0;
  bool readLimitActive_ = false;
  size_t writeLimit_ = 0;
  bool writeLimitActive_ = false;
  bool syncResult_ = true;
  bool seekEndResult_ = true;
  bool renameResult_ = true;
};

#define SdMan SDCardManager::getInstance()
