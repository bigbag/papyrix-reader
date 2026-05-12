#include "XtcProvider.h"

#include <CoverHelpers.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <XtcCoverHelper.h>

#include <cstring>
#include <functional>
#include <string>

namespace papyrix {

Result<void> XtcProvider::open(const char* path, const char* cacheDir) {
  close();

  xtc::XtcError err = parser.open(path);
  if (err != xtc::XtcError::OK) {
    return ErrVoid(Error::ParseFailed);
  }

  // Populate metadata
  meta.clear();
  meta.type = ContentType::Xtc;

  std::string title = parser.getTitle();
  if (title.empty()) {
    const char* lastSlash = strrchr(path, '/');
    const char* filename = lastSlash ? lastSlash + 1 : path;
    utf8SafeCopy(meta.title, sizeof(meta.title), filename);
  } else {
    utf8SafeCopy(meta.title, sizeof(meta.title), title.c_str());
  }

  std::string author = parser.getAuthor();
  if (!author.empty()) {
    utf8SafeCopy(meta.author, sizeof(meta.author), author.c_str());
  } else {
    meta.author[0] = '\0';
  }

  // Create cache path for progress saving
  if (cacheDir && cacheDir[0] != '\0') {
    std::string pathStr(path);
    size_t hash = std::hash<std::string>{}(pathStr);
    snprintf(meta.cachePath, sizeof(meta.cachePath), "%s/xtc_%zu", cacheDir, hash);
    SdMan.mkdir(meta.cachePath);
  } else {
    meta.cachePath[0] = '\0';
  }

  std::string coverPath = getCoverBmpPath();
  strncpy(meta.coverPath, coverPath.c_str(), sizeof(meta.coverPath) - 1);
  meta.coverPath[sizeof(meta.coverPath) - 1] = '\0';

  meta.totalPages = parser.getPageCount();
  meta.currentPage = 0;
  meta.progressPercent = 0;

  return Ok();
}

void XtcProvider::close() {
  parser.close();
  meta.clear();
}

uint32_t XtcProvider::pageCount() const { return parser.getPageCount(); }

uint16_t XtcProvider::tocCount() const { return parser.hasChapters() ? parser.getChapters().size() : 0; }

Result<TocEntry> XtcProvider::getTocEntry(uint16_t index) const {
  if (!parser.hasChapters() || index >= tocCount()) {
    return Err<TocEntry>(Error::InvalidState);
  }

  const auto& chapters = parser.getChapters();
  const auto& chapter = chapters[index];

  TocEntry entry;
  utf8SafeCopy(entry.title, sizeof(entry.title), chapter.name.c_str());
  entry.pageIndex = chapter.startPage;
  entry.depth = 0;  // XTC chapters are flat

  return Ok(entry);
}

std::string XtcProvider::getCoverBmpPath() const { return std::string(meta.cachePath) + "/cover.bmp"; }

std::string XtcProvider::getThumbBmpPath() const { return std::string(meta.cachePath) + "/thumb.bmp"; }

bool XtcProvider::generateCoverBmp() {
  const auto coverPath = getCoverBmpPath();
  if (SdMan.exists(coverPath.c_str())) {
    return true;
  }
  return xtc::generateCoverBmpFromParser(parser, coverPath);
}

bool XtcProvider::generateThumbBmp() {
  const auto thumbPath = getThumbBmpPath();
  const auto failedMarkerPath = std::string(meta.cachePath) + "/.thumb.failed";

  if (SdMan.exists(thumbPath.c_str())) return true;

  if (SdMan.exists(failedMarkerPath.c_str())) {
    return false;
  }

  if (!SdMan.exists(getCoverBmpPath().c_str()) && !generateCoverBmp()) {
    FsFile marker;
    if (SdMan.openFileForWrite("XTC", failedMarkerPath, marker)) {
      marker.close();
    }
    return false;
  }

  const bool success = CoverHelpers::generateThumbFromCover(getCoverBmpPath(), thumbPath, "XTC");
  if (!success) {
    FsFile marker;
    if (SdMan.openFileForWrite("XTC", failedMarkerPath, marker)) {
      marker.close();
    }
  }
  return success;
}

}  // namespace papyrix
