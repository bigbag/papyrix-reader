#include "MarkdownProvider.h"

#include <Utf8.h>

#include <cstring>

namespace papyrix {

Result<void> MarkdownProvider::open(const char* path, const char* cacheDir) {
  close();

  markdown.reset(new Markdown(path, cacheDir));

  if (!markdown->load()) {
    markdown.reset();
    return ErrVoid(Error::ParseFailed);
  }

  // Populate metadata
  meta.clear();
  meta.type = ContentType::Markdown;

  const std::string& title = markdown->getTitle();
  utf8SafeCopy(meta.title, sizeof(meta.title), title.c_str());

  meta.author[0] = '\0';  // Markdown doesn't have author

  const std::string& cachePath = markdown->getCachePath();
  strncpy(meta.cachePath, cachePath.c_str(), sizeof(meta.cachePath) - 1);
  meta.cachePath[sizeof(meta.cachePath) - 1] = '\0';

  // Cover path
  std::string coverPath = markdown->getCoverBmpPath();
  strncpy(meta.coverPath, coverPath.c_str(), sizeof(meta.coverPath) - 1);
  meta.coverPath[sizeof(meta.coverPath) - 1] = '\0';

  // Markdown uses file size, not pages
  meta.totalPages = 1;  // Will be updated by reader
  meta.currentPage = 0;
  meta.progressPercent = 0;

  return Ok();
}

void MarkdownProvider::close() {
  markdown.reset();
  meta.clear();
}

uint32_t MarkdownProvider::pageCount() const {
  if (!markdown) return 0;

  // Estimate pages based on file size (same as TXT)
  constexpr size_t BYTES_PER_PAGE = 2048;
  size_t fileSize = markdown->getFileSize();
  return (fileSize + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE;
}

}  // namespace papyrix
