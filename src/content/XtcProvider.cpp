#include "XtcProvider.h"

#include <HardwareSerial.h>
#include <HomeThumbnail.h>
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

    // One-time invalidation of failure markers persisted by the old
    // page-buffer cover generator (structural 96KB allocation failure).
    xtc::migrateStaleFailureMarkers(meta.cachePath, parser.getBitDepth() == 2);
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

bool XtcProvider::generateCoverBmp(const std::function<bool()>& shouldAbort) {
  const std::string coverPath = getCoverBmpPath();
  const std::string failedMarkerPath = std::string(meta.cachePath) + "/.cover.failed";
  if (home_thumbnail::validateCover(coverPath)) {
    SdMan.remove(failedMarkerPath.c_str());
    return true;
  }
  if (SdMan.exists(coverPath.c_str())) SdMan.remove(coverPath.c_str());
  if (SdMan.exists(failedMarkerPath.c_str())) return false;

  const xtc::CoverResult generated = xtc::generateCoverBmpFromParser(parser, coverPath, shouldAbort);
  const bool cancelled = shouldAbort && shouldAbort();
  const bool valid = generated == xtc::CoverResult::Generated && !cancelled && home_thumbnail::validateCover(coverPath);
  if (cancelled) {
    SdMan.remove(coverPath.c_str());
  } else if (valid) {
    SdMan.remove(failedMarkerPath.c_str());
  } else if (generated == xtc::CoverResult::InvalidFile) {
    FsFile marker;
    if (SdMan.openFileForWrite("XTC", failedMarkerPath, marker)) marker.close();
  } else {
    // Transient failure (heap/IO): no marker so a later attempt can retry.
    SdMan.remove(coverPath.c_str());
  }
  return valid;
}

}  // namespace papyrix
