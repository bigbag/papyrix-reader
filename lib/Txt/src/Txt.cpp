/**
 * Txt.cpp
 *
 * Plain text file handler implementation for Papyrix Reader
 */

#include "Txt.h"

#include <CoverHelpers.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <SDCardManager.h>

#define TAG "TXT"

Txt::Txt(std::string filepath, const std::string& cacheDir)
    : filepath(std::move(filepath)), fileSize(0), loaded(false) {
  // Create cache key based on filepath (same as Epub/Xtc)
  cachePath = cacheDir + "/txt_" + std::to_string(std::hash<std::string>{}(this->filepath));

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

bool Txt::load() {
  LOG_INF(TAG, "Loading TXT: %s", filepath.c_str());

  if (!SdMan.exists(filepath.c_str())) {
    LOG_ERR(TAG, "File does not exist");
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("TXT", filepath, file)) {
    LOG_ERR(TAG, "Failed to open file");
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_INF(TAG, "Loaded TXT: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

bool Txt::clearCache() const {
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

void Txt::setupCacheDir() const {
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

std::string Txt::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Txt::findCoverImage() const {
  // Extract directory path
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";

  return CoverHelpers::findCoverImage(dirPath, title);
}

bool Txt::generateCoverBmp(bool use1BitDithering) const {
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
    if (SdMan.openFileForWrite("TXT", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Convert to BMP using shared helper
  const bool success = CoverHelpers::convertImageToBmp(coverImagePath, coverPath, "TXT", use1BitDithering);
  if (!success) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("TXT", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  return success;
}

std::string Txt::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

bool Txt::generateThumbBmp() const {
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
    if (SdMan.openFileForWrite("TXT", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  setupCacheDir();

  const bool success = CoverHelpers::generateThumbFromCover(getCoverBmpPath(), thumbPath, "TXT");
  if (!success) {
    // Create failure marker
    FsFile marker;
    if (SdMan.openFileForWrite("TXT", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  return success;
}

size_t Txt::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return 0;
  }

  FsFile file;
  if (!SdMan.openFileForRead("TXT", filepath, file)) {
    return 0;
  }

  if (offset > 0) {
    file.seek(offset);
  }

  size_t bytesRead = file.read(buffer, length);
  file.close();

  return bytesRead;
}
