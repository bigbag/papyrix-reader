#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "../core/Result.h"

// Forward declare SdFat types
class FsFile;

namespace papyrix {
namespace drivers {

class Storage {
 public:
  Result<void> init();
  void shutdown();

  bool isMounted() const { return mounted_; }

  // File operations
  Result<void> openRead(const char* path, FsFile& out);
  Result<void> openWrite(const char* path, FsFile& out);
  Result<bool> exists(const char* path);
  Result<void> remove(const char* path);
  Result<void> mkdir(const char* path);
  using RemoveDirProgress = std::function<void(int filesDeleted)>;
  Result<void> rmdir(const char* path, RemoveDirProgress progress = nullptr);

  // Directory operations
  Result<void> openDir(const char* path, FsFile& out);

  // Utility
  Result<size_t> readToBuffer(const char* path, char* buffer, size_t bufferSize);

 private:
  bool mounted_ = false;
};

}  // namespace drivers
}  // namespace papyrix
