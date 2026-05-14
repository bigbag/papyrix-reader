#include "SDCardManager.h"

#include <Logging.h>

#define TAG "SD"

namespace {
constexpr uint8_t SD_CS = 12;
constexpr uint32_t SPI_FQ = 40000000;
}  // namespace

SDCardManager SDCardManager::instance;

SDCardManager::SDCardManager() : sd() {}

bool SDCardManager::begin() {
  if (!sd.begin(SD_CS, SPI_FQ)) {
    LOG_ERR(TAG, "SD card not detected");
    initialized = false;
  } else {
    LOG_INF(TAG, "SD card detected");
    initialized = true;
  }

  return initialized;
}

void SDCardManager::end() {
  if (initialized) {
    sd.end();
    initialized = false;
  }
}

bool SDCardManager::ready() const { return initialized; }

std::vector<String> SDCardManager::listFiles(const char* path, const int maxFiles) {
  std::vector<String> ret;
  if (!initialized) {
    LOG_ERR(TAG, "not initialized, returning empty list");
    return ret;
  }

  auto root = sd.open(path);
  if (!root) {
    LOG_ERR(TAG, "Failed to open directory");
    return ret;
  }
  if (!root.isDirectory()) {
    LOG_ERR(TAG, "Path is not a directory");
    root.close();
    return ret;
  }

  int count = 0;
  char name[128];
  auto f = root.openNextFile();
  while (f && count < maxFiles) {
    if (f.isDirectory()) {
      f.close();
      f = root.openNextFile();
      continue;
    }
    f.getName(name, sizeof(name));
    ret.emplace_back(name);
    f.close();
    count++;
    f = root.openNextFile();
  }
  if (f) f.close();
  root.close();
  return ret;
}

String SDCardManager::readFile(const char* path) {
  if (!initialized) {
    LOG_ERR(TAG, "not initialized; cannot read file");
    return {""};
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    return {""};
  }

  constexpr size_t maxSize = 50000;  // Limit to 50KB
  const size_t fileSize = f.size();
  const size_t toRead = (fileSize < maxSize) ? fileSize : maxSize;

  String content;
  content.reserve(toRead);

  uint8_t buf[1024];
  size_t readSize = 0;
  while (f.available() && readSize < toRead) {
    const size_t chunkSize = min(sizeof(buf), toRead - readSize);
    const int n = f.read(buf, chunkSize);
    if (n <= 0) break;
    content.concat(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    readSize += static_cast<size_t>(n);
  }
  f.close();
  return content;
}

bool SDCardManager::readFileToStream(const char* path, Print& out, const size_t chunkSize) {
  if (!initialized) {
    LOG_ERR(TAG, "SD card not initialized");
    return false;
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    return false;
  }

  constexpr size_t localBufSize = 1024;
  uint8_t buf[localBufSize];
  const size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    const int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, static_cast<size_t>(r));
    } else {
      break;
    }
  }

  f.close();
  return true;
}

size_t SDCardManager::readFileToBuffer(const char* path, char* buffer, const size_t bufferSize, const size_t maxBytes) {
  if (!buffer || bufferSize == 0) return 0;
  if (!initialized) {
    LOG_ERR(TAG, "SD card not initialized");
    buffer[0] = '\0';
    return 0;
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    buffer[0] = '\0';
    return 0;
  }

  const size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;

  while (f.available() && total < maxToRead) {
    constexpr size_t chunk = 64;
    const size_t want = maxToRead - total;
    const size_t readLen = (want < chunk) ? want : chunk;
    const int r = f.read(buffer + total, readLen);
    if (r > 0) {
      total += static_cast<size_t>(r);
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool SDCardManager::writeFile(const char* path, const String& content) {
  if (!initialized) {
    LOG_ERR(TAG, "SD card not initialized");
    return false;
  }

  // Remove existing file so we perform an overwrite rather than append
  if (sd.exists(path)) {
    sd.remove(path);
  }

  FsFile f;
  if (!openFileForWrite("SD", path, f)) {
    return false;
  }

  const size_t written = f.print(content);
  f.close();
  return written == content.length();
}

bool SDCardManager::ensureDirectoryExists(const char* path) {
  if (!initialized) {
    LOG_ERR(TAG, "SD card not initialized");
    return false;
  }

  // Check if directory already exists
  if (sd.exists(path)) {
    FsFile dir = sd.open(path);
    if (dir && dir.isDirectory()) {
      dir.close();
      return true;
    }
    dir.close();
  }

  // Retry mkdir on transient failures (FAT directory cluster cache eviction
  // races with the directory walk under memory pressure).
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      delay(50);
      // Short-circuit: a previous attempt may have succeeded but returned false
      if (sd.exists(path)) {
        FsFile dir = sd.open(path);
        if (dir && dir.isDirectory()) {
          dir.close();
          return true;
        }
        dir.close();
      }
    }
    if (sd.mkdir(path)) {
      LOG_INF(TAG, "Created directory: %s", path);
      return true;
    }
  }

  LOG_ERR(TAG, "Failed to create directory: %s", path);
  return false;
}

bool SDCardManager::openFileForRead(const char* moduleName, const char* path, FsFile& file) {
  // SdFat's directory cache occasionally returns false-negatives under memory
  // pressure — observed moments after a successful write. Drop the redundant
  // sd.exists() precheck (sd.open() does its own lookup) and retry with a
  // brief delay to let the FAT cache settle.
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) delay(50);
    file = sd.open(path, O_RDONLY);
    if (file) {
      return true;
    }
  }
  LOG_DBG(moduleName, "File does not exist: %s", path);
  return false;
}

bool SDCardManager::openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForRead(const char* moduleName, const String& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
  // Same defensive retry as openFileForRead — under memory pressure SdFat
  // occasionally fails to allocate a directory entry slot on the first try.
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) delay(50);
    file = sd.open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (file) {
      return true;
    }
  }
  LOG_ERR(moduleName, "Failed to open file for writing: %s", path);
  return false;
}

bool SDCardManager::openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForWrite(const char* moduleName, const String& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool SDCardManager::removeDir(const char* path, RemoveDirProgress progress) {
  struct Node {
    String path;
    Node* next;
  };

  auto freeList = [](Node*& head) {
    while (head) {
      auto* n = head->next;
      delete head;
      head = n;
    }
  };

  Node* dirs = nullptr;
  Node* stack = new (std::nothrow) Node{String(path), nullptr};
  if (!stack) return false;

  bool allOk = true;
  int deleted = 0;
  char name[128];
  constexpr int kMaxEntries = 4096;

  while (stack) {
    Node* top = stack;
    String dirPath = std::move(top->path);
    stack = top->next;
    delete top;

    auto* dirNode = new (std::nothrow) Node{dirPath, dirs};
    if (!dirNode) {
      freeList(dirs);
      freeList(stack);
      return false;
    }
    dirs = dirNode;

    auto dir = sd.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }

    int iterations = 0;
    auto file = dir.openNextFile();
    while (file && iterations++ < kMaxEntries) {
      file.getName(name, sizeof(name));
      if (name[0] == '\0' || (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))) {
        file.close();
        file = dir.openNextFile();
        continue;
      }

      String filePath = dirPath;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += name;

      const bool isDir = file.isDirectory();
      file.close();

      if (isDir) {
        auto* node = new (std::nothrow) Node{std::move(filePath), stack};
        if (!node) {
          dir.close();
          freeList(dirs);
          freeList(stack);
          return false;
        }
        stack = node;
      } else {
        if (!sd.remove(filePath.c_str())) allOk = false;
        deleted++;
        if ((deleted % 10) == 0) {
          delay(1);
          if (progress) progress(deleted);
        }
      }

      file = dir.openNextFile();
    }
    dir.close();
  }

  if (progress) progress(deleted);

  while (dirs) {
    if (!sd.rmdir(dirs->path.c_str())) allOk = false;
    auto* n = dirs->next;
    delete dirs;
    dirs = n;
  }

  return allOk;
}
