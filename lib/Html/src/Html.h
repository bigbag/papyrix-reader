#pragma once

#include <functional>
#include <string>

class Html {
  std::string filepath;
  std::string cachePath;
  std::string title;
  size_t fileSize;
  bool loaded;

 public:
  explicit Html(std::string filepath, const std::string& cacheDir);
  ~Html() = default;

  bool load();
  bool clearCache() const;
  void setupCacheDir() const;

  const std::string& getCachePath() const { return cachePath; }
  const std::string& getPath() const { return filepath; }
  const std::string& getTitle() const { return title; }
  size_t getFileSize() const { return fileSize; }

  std::string getCoverBmpPath() const;
  bool generateCoverBmp(bool use1BitDithering = false, const std::function<bool()>& shouldAbort = nullptr) const;
  std::string findCoverImage(const std::function<bool()>& shouldAbort = nullptr) const;

  bool isLoaded() const { return loaded; }
};
