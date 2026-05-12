#include "EpubProvider.h"

#include <Utf8.h>

#include <cstring>

namespace papyrix {

Result<void> EpubProvider::open(const char* path, const char* cacheDir) {
  close();

  epub = std::make_shared<Epub>(path, cacheDir);

  if (!epub->load()) {
    epub.reset();
    return ErrVoid(Error::ParseFailed);
  }

  // Populate metadata
  meta.clear();
  meta.type = ContentType::Epub;

  const std::string& title = epub->getTitle();
  utf8SafeCopy(meta.title, sizeof(meta.title), title.c_str());

  const std::string& author = epub->getAuthor();
  utf8SafeCopy(meta.author, sizeof(meta.author), author.c_str());

  const std::string& cachePath = epub->getCachePath();
  strncpy(meta.cachePath, cachePath.c_str(), sizeof(meta.cachePath) - 1);
  meta.cachePath[sizeof(meta.cachePath) - 1] = '\0';

  // Cover path
  std::string coverPath = epub->getCoverBmpPath();
  strncpy(meta.coverPath, coverPath.c_str(), sizeof(meta.coverPath) - 1);
  meta.coverPath[sizeof(meta.coverPath) - 1] = '\0';

  meta.totalPages = epub->getSpineItemsCount();
  meta.currentPage = 0;
  meta.progressPercent = 0;

  return Ok();
}

void EpubProvider::close() {
  epub.reset();
  meta.clear();
}

uint32_t EpubProvider::pageCount() const { return epub ? epub->getSpineItemsCount() : 0; }

uint16_t EpubProvider::tocCount() const { return epub ? epub->getTocItemsCount() : 0; }

Result<TocEntry> EpubProvider::getTocEntry(uint16_t index) const {
  if (!epub || index >= tocCount()) {
    return Err<TocEntry>(Error::InvalidState);
  }

  auto tocItem = epub->getTocItem(index);
  TocEntry entry;
  utf8SafeCopy(entry.title, sizeof(entry.title), tocItem.title.c_str());
  entry.pageIndex = epub->getSpineIndexForTocIndex(index);
  entry.depth = tocItem.level;

  return Ok(entry);
}

}  // namespace papyrix
