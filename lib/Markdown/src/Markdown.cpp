/**
 * Markdown.cpp
 *
 * Markdown file handler implementation for Papyrix Reader
 */

#include "Markdown.h"

#include <CoverHelpers.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <SDCardManager.h>

#define TAG "MARKDOWN"

Markdown::Markdown(std::string filepath, const std::string& cacheDir)
    : filepath(std::move(filepath)), fileSize(0), loaded(false) {
  // Create cache key based on filepath (same as Epub/Xtc/Txt)
  cachePath = cacheDir + "/md_" + std::to_string(std::hash<std::string>{}(this->filepath));

  // Extract title from filename
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

bool Markdown::load() {
  LOG_INF(TAG, "Loading Markdown: %s", filepath.c_str());

  if (!SdMan.exists(filepath.c_str())) {
    LOG_ERR(TAG, "File does not exist");
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("MD ", filepath, file)) {
    LOG_ERR(TAG, "Failed to open file");
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;

  // Try to extract title from content (updates title member if found)
  extractTitleFromContent();

  LOG_INF(TAG, "Loaded Markdown: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

bool Markdown::clearCache() const {
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

void Markdown::setupCacheDir() const {
  if (SdMan.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      SdMan.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  SdMan.mkdir(cachePath.c_str());
}

std::string Markdown::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Markdown::findCoverImage() const {
  // Extract directory path
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";

  return CoverHelpers::findCoverImage(dirPath, title);
}

bool Markdown::generateCoverBmp(bool use1BitDithering) const {
  const auto coverPath = getCoverBmpPath();
  const auto failedMarkerPath = cachePath + "/.cover.failed";

  // Already generated
  if (SdMan.exists(coverPath.c_str())) {
    return true;
  }

  // Previously failed, don't retry
  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  // Find a cover image
  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    LOG_DBG(TAG, "No cover image found");
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("MD ", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Convert to BMP using shared helper
  const bool success = CoverHelpers::convertImageToBmp(coverImagePath, coverPath, "MD ", use1BitDithering);
  if (!success) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("MD ", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  return success;
}

std::string Markdown::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

bool Markdown::generateThumbBmp() const {
  const auto thumbPath = getThumbBmpPath();
  const auto failedMarkerPath = cachePath + "/.thumb.failed";

  if (SdMan.exists(thumbPath.c_str())) return true;

  // Previously failed, don't retry
  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  if (!SdMan.exists(getCoverBmpPath().c_str()) && !generateCoverBmp(true)) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("MD ", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  setupCacheDir();

  const bool success = CoverHelpers::generateThumbFromCover(getCoverBmpPath(), thumbPath, "MD ");
  if (!success) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("MD ", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  return success;
}

size_t Markdown::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return 0;
  }

  FsFile file;
  if (!SdMan.openFileForRead("MD ", filepath, file)) {
    return 0;
  }

  if (offset > 0) {
    file.seek(offset);
  }

  size_t bytesRead = file.read(buffer, length);
  file.close();

  return bytesRead;
}

bool Markdown::extractTitleFromContent() {
  // Check cache first
  std::string titleCachePath = getTitleCachePath();
  if (SdMan.exists(titleCachePath.c_str())) {
    FsFile file;
    if (SdMan.openFileForRead("MD ", titleCachePath, file)) {
      char buf[128];
      int len = file.read(buf, sizeof(buf) - 1);
      file.close();
      if (len > 0) {
        buf[len] = '\0';
        title = buf;
        return true;
      }
    }
  }

  // Read first 4KB - use heap instead of stack to avoid overflow on ESP32-C3
  constexpr size_t SCAN_SIZE = 4096;
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[SCAN_SIZE]);
  if (!buffer) return false;

  size_t bytesRead = readContent(buffer.get(), 0, SCAN_SIZE);
  if (bytesRead == 0) return false;

  // Scan for ATX header (# Title)
  std::string extracted;
  const char* p = reinterpret_cast<const char*>(buffer.get());
  const char* end = p + bytesRead;

  while (p < end) {
    // Skip to start of line
    while (p < end && (*p == '\n' || *p == '\r')) p++;
    if (p >= end) break;

    const char* lineStart = p;
    // Find end of line
    while (p < end && *p != '\n' && *p != '\r') p++;
    size_t lineLen = static_cast<size_t>(p - lineStart);

    // Check for ATX header
    if (lineLen > 1 && lineStart[0] == '#') {
      size_t hashCount = 0;
      while (hashCount < lineLen && lineStart[hashCount] == '#') hashCount++;

      if (hashCount <= 6 && hashCount < lineLen && lineStart[hashCount] == ' ') {
        // Extract title text - skip all leading whitespace after #
        size_t start = hashCount;
        while (start < lineLen && lineStart[start] == ' ') start++;
        size_t titleEnd = lineLen;
        // Strip trailing # and spaces
        while (titleEnd > start && (lineStart[titleEnd - 1] == '#' || lineStart[titleEnd - 1] == ' ')) titleEnd--;

        if (titleEnd > start) {
          extracted = std::string(lineStart + start, titleEnd - start);
          break;
        }
      }
    }
  }

  if (extracted.empty()) return false;

  // Truncate to fit buffer
  if (extracted.length() > 127) extracted.resize(127);

  // Update title
  title = extracted;

  // Cache to SD
  setupCacheDir();
  FsFile file;
  if (SdMan.openFileForWrite("MD ", titleCachePath, file)) {
    file.write(reinterpret_cast<const uint8_t*>(title.c_str()), title.length());
    file.close();
  }

  return true;
}
