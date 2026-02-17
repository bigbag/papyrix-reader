#include "Fb2Provider.h"

#include <cstring>

namespace papyrix {

Result<void> Fb2Provider::open(const char* path, const char* cacheDir) {
  close();

  fb2.reset(new Fb2(path, cacheDir));

  if (!fb2->load()) {
    fb2.reset();
    return ErrVoid(Error::ParseFailed);
  }

  // Populate metadata
  meta.clear();
  meta.type = ContentType::Fb2;

  const std::string& title = fb2->getTitle();
  strncpy(meta.title, title.c_str(), sizeof(meta.title) - 1);
  meta.title[sizeof(meta.title) - 1] = '\0';

  const std::string& author = fb2->getAuthor();
  strncpy(meta.author, author.c_str(), sizeof(meta.author) - 1);
  meta.author[sizeof(meta.author) - 1] = '\0';

  const std::string& cachePath = fb2->getCachePath();
  strncpy(meta.cachePath, cachePath.c_str(), sizeof(meta.cachePath) - 1);
  meta.cachePath[sizeof(meta.cachePath) - 1] = '\0';

  // Cover path
  std::string coverPath = fb2->getCoverBmpPath();
  strncpy(meta.coverPath, coverPath.c_str(), sizeof(meta.coverPath) - 1);
  meta.coverPath[sizeof(meta.coverPath) - 1] = '\0';

  // FB2 uses file size, not pages (pages calculated during rendering)
  meta.totalPages = 1;  // Will be updated by reader
  meta.currentPage = 0;
  meta.progressPercent = 0;

  return Ok();
}

void Fb2Provider::close() {
  fb2.reset();
  meta.clear();
}

uint32_t Fb2Provider::pageCount() const {
  if (!fb2) return 0;

  // Estimate pages based on file size
  constexpr size_t BYTES_PER_PAGE = 2048;
  size_t fileSize = fb2->getFileSize();
  return (fileSize + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE;
}

uint16_t Fb2Provider::tocCount() const {
  if (!fb2) return 0;
  return fb2->tocCount();
}

Result<TocEntry> Fb2Provider::getTocEntry(uint16_t index) const {
  if (!fb2 || index >= fb2->tocCount()) {
    return Err<TocEntry>(Error::InvalidState);
  }

  const Fb2::TocItem& item = fb2->getTocItem(index);

  TocEntry entry;
  strncpy(entry.title, item.title.c_str(), sizeof(entry.title) - 1);
  entry.title[sizeof(entry.title) - 1] = '\0';
  entry.pageIndex = item.sectionIndex;
  entry.depth = 0;

  return Ok(entry);
}

}  // namespace papyrix
