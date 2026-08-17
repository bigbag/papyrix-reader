#include "Html.h"

#include <CoverHelpers.h>
#include <FsHelpers.h>
#include <HomeThumbnail.h>
#include <Logging.h>
#include <SDCardManager.h>

#include <cctype>
#include <cstring>

#define TAG "HTML"

Html::Html(std::string filepath, const std::string& cacheDir)
    : filepath(std::move(filepath)), fileSize(0), loaded(false) {
  cachePath = cacheDir + "/html_" + std::to_string(std::hash<std::string>{}(this->filepath));

  // Extract title from filename (fallback, may be overridden by <title> tag)
  size_t lastSlash = this->filepath.find_last_of('/');
  size_t lastDot = this->filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    title = this->filepath.substr(lastSlash);
  } else {
    title = this->filepath.substr(lastSlash, lastDot - lastSlash);
  }
}

bool Html::load() {
  LOG_INF(TAG, "Loading HTML: %s", filepath.c_str());

  if (!SdMan.exists(filepath.c_str())) {
    LOG_ERR(TAG, "File does not exist");
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("HTML", filepath, file)) {
    LOG_ERR(TAG, "Failed to open file");
    return false;
  }

  fileSize = file.size();

  // Try to extract <title> from first 4KB (heap-allocated to avoid stack pressure)
  constexpr size_t SCAN_SIZE = 4096;
  if (fileSize > 0) {
    auto* buf = new (std::nothrow) char[SCAN_SIZE + 1];
    if (buf) {
      size_t toRead = fileSize < SCAN_SIZE ? fileSize : SCAN_SIZE;
      size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(buf), toRead);
      buf[bytesRead] = '\0';

      // Case-insensitive search for <title>...</title>
      // Convert to lowercase for searching
      auto* lower = new (std::nothrow) char[bytesRead + 1];
      if (lower) {
        for (size_t i = 0; i < bytesRead; i++) {
          lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(buf[i])));
        }
        lower[bytesRead] = '\0';

        const char* titleStart = strstr(lower, "<title>");
        if (titleStart) {
          size_t startOffset = static_cast<size_t>(titleStart - lower) + 7;  // skip "<title>"
          const char* titleEnd = strstr(lower + startOffset, "</title>");
          if (titleEnd) {
            size_t endOffset = static_cast<size_t>(titleEnd - lower);
            if (endOffset > startOffset) {
              size_t len = endOffset - startOffset;
              if (len > 255) len = 255;
              // Use original case from buf
              std::string extracted(buf + startOffset, len);
              // Trim whitespace
              size_t first = extracted.find_first_not_of(" \t\r\n");
              size_t last = extracted.find_last_not_of(" \t\r\n");
              if (first != std::string::npos) {
                title = extracted.substr(first, last - first + 1);
              }
            }
          }
        }
        delete[] lower;
      }
      delete[] buf;
    }
  }

  file.close();

  loaded = true;
  LOG_INF(TAG, "Loaded HTML: %s (%zu bytes), title: %s", filepath.c_str(), fileSize, title.c_str());
  return true;
}

bool Html::clearCache() const {
  if (!SdMan.exists(cachePath.c_str())) {
    LOG_DBG(TAG, "Cache does not exist, no action needed");
    return true;
  }

  if (!SdMan.removeDir(cachePath.c_str())) {
    LOG_ERR(TAG, "Failed to clear cache");
    return false;
  }

  LOG_INF(TAG, "Cache cleared successfully");
  return true;
}

void Html::setupCacheDir() const {
  if (SdMan.exists(cachePath.c_str())) {
    return;
  }

  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      SdMan.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  SdMan.mkdir(cachePath.c_str());
}

std::string Html::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Html::findCoverImage(const std::function<bool()>& shouldAbort) const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";

  return CoverHelpers::findCoverImage(dirPath, title, shouldAbort);
}

bool Html::generateCoverBmp(bool use1BitDithering, const std::function<bool()>& shouldAbort) const {
  const auto coverPath = getCoverBmpPath();
  const auto failedMarkerPath = cachePath + "/.cover.failed";

  if (home_thumbnail::validateCover(coverPath)) {
    SdMan.remove(failedMarkerPath.c_str());
    return true;
  }
  if (SdMan.exists(coverPath.c_str())) SdMan.remove(coverPath.c_str());
  if (SdMan.exists(failedMarkerPath.c_str())) return false;

  std::string coverImagePath = findCoverImage(shouldAbort);
  if (coverImagePath.empty()) {
    LOG_DBG(TAG, "No cover image found");
    if (!CoverHelpers::isAbortRequested(shouldAbort)) {
      FsFile marker;
      if (SdMan.openFileForWrite("HTML", failedMarkerPath, marker)) {
        marker.close();
      }
    }
    return false;
  }

  setupCacheDir();

  const bool success =
      CoverHelpers::convertImageToBmp(coverImagePath, coverPath, "HTML", use1BitDithering, shouldAbort);
  const bool cancelled = CoverHelpers::isAbortRequested(shouldAbort);
  const bool valid = success && !cancelled && home_thumbnail::validateCover(coverPath);
  if (cancelled) {
    SdMan.remove(coverPath.c_str());
  } else if (!valid) {
    FsFile marker;
    if (SdMan.openFileForWrite("HTML", failedMarkerPath, marker)) marker.close();
  } else {
    SdMan.remove(failedMarkerPath.c_str());
  }
  return valid;
}
