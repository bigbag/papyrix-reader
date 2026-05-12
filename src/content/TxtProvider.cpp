#include "TxtProvider.h"

#include <Utf8.h>

#include <cstring>

namespace papyrix {

Result<void> TxtProvider::open(const char* path, const char* cacheDir) {
  close();

  txt.reset(new Txt(path, cacheDir));

  if (!txt->load()) {
    txt.reset();
    return ErrVoid(Error::ParseFailed);
  }

  // Populate metadata
  meta.clear();
  meta.type = ContentType::Txt;

  const std::string& title = txt->getTitle();
  utf8SafeCopy(meta.title, sizeof(meta.title), title.c_str());

  meta.author[0] = '\0';  // TXT doesn't have author

  const std::string& cachePath = txt->getCachePath();
  strncpy(meta.cachePath, cachePath.c_str(), sizeof(meta.cachePath) - 1);
  meta.cachePath[sizeof(meta.cachePath) - 1] = '\0';

  // Cover path
  std::string coverPath = txt->getCoverBmpPath();
  strncpy(meta.coverPath, coverPath.c_str(), sizeof(meta.coverPath) - 1);
  meta.coverPath[sizeof(meta.coverPath) - 1] = '\0';

  // TXT uses file size, not pages (pages calculated during rendering)
  meta.totalPages = 1;  // Will be updated by reader
  meta.currentPage = 0;
  meta.progressPercent = 0;

  return Ok();
}

void TxtProvider::close() {
  txt.reset();
  meta.clear();
}

uint32_t TxtProvider::pageCount() const {
  if (!txt) return 0;

  // Estimate pages based on file size
  // Each page shows approximately 2KB of text
  constexpr size_t BYTES_PER_PAGE = 2048;
  size_t fileSize = txt->getFileSize();
  return (fileSize + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE;
}

}  // namespace papyrix
