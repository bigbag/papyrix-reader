/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <FsHelpers.h>
#include <HomeThumbnail.h>
#include <Logging.h>
#include <SDCardManager.h>

#define TAG "XTC"
#include <XtcCoverHelper.h>

bool Xtc::load() {
  LOG_INF(TAG, "Loading XTC: %s", filepath.c_str());

  // Initialize parser
  parser.reset(new xtc::XtcParser());

  // Open XTC file
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    LOG_ERR(TAG, "Failed to load: %s", xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  LOG_INF(TAG, "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
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

void Xtc::setupCacheDir() const {
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

void Xtc::migrateStaleFailureMarkers() const {
  xtc::migrateStaleFailureMarkers(cachePath, parser && parser->getBitDepth() == 2);
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() const {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  const std::string coverPath = getCoverBmpPath();
  const std::string failedMarkerPath = cachePath + "/.cover.failed";
  if (home_thumbnail::validateCover(coverPath)) {
    SdMan.remove(failedMarkerPath.c_str());
    return true;
  }
  if (SdMan.exists(coverPath.c_str())) SdMan.remove(coverPath.c_str());
  if (SdMan.exists(failedMarkerPath.c_str())) return false;

  if (!loaded || !parser) {
    LOG_ERR(TAG, "Cannot generate cover BMP, file not loaded");
    FsFile marker;
    if (SdMan.openFileForWrite("XTC", failedMarkerPath, marker)) marker.close();
    return false;
  }

  setupCacheDir();
  migrateStaleFailureMarkers();
  const xtc::CoverResult generated =
      xtc::generateCoverBmpFromParser(*const_cast<xtc::XtcParser*>(parser.get()), coverPath);
  const bool valid = generated == xtc::CoverResult::Generated && home_thumbnail::validateCover(coverPath);
  if (!valid && generated == xtc::CoverResult::InvalidFile) {
    FsFile marker;
    if (SdMan.openFileForWrite("XTC", failedMarkerPath, marker)) marker.close();
  } else if (!valid) {
    // Transient failure (heap/IO): no marker so a later attempt can retry.
    SdMan.remove(coverPath.c_str());
  } else {
    SdMan.remove(failedMarkerPath.c_str());
  }
  return valid;
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex, xtc::XtcParser::PageChunkCallback callback, size_t chunkSize,
                                     const std::function<bool()>& shouldAbort) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize, shouldAbort);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
