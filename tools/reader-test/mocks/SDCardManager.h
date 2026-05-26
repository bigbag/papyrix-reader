#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "SdFat.h"

class SDCardManager {
 public:
  SDCardManager() = default;
  bool begin() { return true; }
  bool ready() const { return true; }

  bool exists(const char* path) { return access(path, F_OK) == 0; }

  FsFile open(const char* path, int mode = O_RDONLY) {
    FsFile file;
    file.open(path, mode);
    return file;
  }

  bool openFileForRead(const char* moduleName, const char* path, FsFile& file) {
    (void)moduleName;
    return file.open(path, O_RDONLY);
  }

  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
    return openFileForRead(moduleName, path.c_str(), file);
  }

  bool openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
    (void)moduleName;
    return file.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  }

  bool openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
    return openFileForWrite(moduleName, path.c_str(), file);
  }

  bool remove(const char* path) { return ::remove(path) == 0; }

  bool rename(const char* oldPath, const char* newPath) { return ::rename(oldPath, newPath) == 0; }

  using RemoveDirProgress = std::function<void(int filesDeleted)>;
  bool removeDir(const char* path, RemoveDirProgress progress = nullptr) {
    (void)progress;
    std::string cmd = "rm -rf '";
    cmd += path;
    cmd += "'";
    return system(cmd.c_str()) == 0;
  }

  bool mkdir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) return true;
    return ::mkdir(path, 0755) == 0;
  }

  static SDCardManager& getInstance() {
    static SDCardManager instance;
    return instance;
  }
};

#define SdMan SDCardManager::getInstance()
