#pragma once
#include <SDCardManager.h>

#include <functional>
#include <string>

class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t, size_t)>;
  // Chunk callback: receives data and length, returns true to continue, false to abort
  using ChunkCallback = std::function<bool(const char*, size_t)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  // Streaming fetch: calls onChunk for each chunk of data received
  // Returns true if completed successfully, false on error or abort
  static bool fetchUrlStreaming(const std::string& url, ChunkCallback onChunk, const std::string& username = "",
                                const std::string& password = "");

  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, const std::string& username = "",
                                      const std::string& password = "");

 private:
  static constexpr size_t DOWNLOAD_CHUNK_SIZE = 1024;
};
