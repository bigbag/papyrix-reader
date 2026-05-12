#include "HtmlProvider.h"

#include <Utf8.h>

#include <cstring>

namespace papyrix {

Result<void> HtmlProvider::open(const char* path, const char* cacheDir) {
  close();

  html.reset(new Html(path, cacheDir));

  if (!html->load()) {
    html.reset();
    return ErrVoid(Error::ParseFailed);
  }

  meta.clear();
  meta.type = ContentType::Html;

  const std::string& title = html->getTitle();
  utf8SafeCopy(meta.title, sizeof(meta.title), title.c_str());

  meta.author[0] = '\0';

  const std::string& cachePath = html->getCachePath();
  strncpy(meta.cachePath, cachePath.c_str(), sizeof(meta.cachePath) - 1);
  meta.cachePath[sizeof(meta.cachePath) - 1] = '\0';

  std::string coverPath = html->getCoverBmpPath();
  strncpy(meta.coverPath, coverPath.c_str(), sizeof(meta.coverPath) - 1);
  meta.coverPath[sizeof(meta.coverPath) - 1] = '\0';

  meta.totalPages = 1;  // Will be updated by reader
  meta.currentPage = 0;
  meta.progressPercent = 0;

  return Ok();
}

void HtmlProvider::close() {
  html.reset();
  meta.clear();
}

uint32_t HtmlProvider::pageCount() const {
  if (!html) return 0;

  constexpr size_t BYTES_PER_PAGE = 2048;
  size_t fileSize = html->getFileSize();
  return (fileSize + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE;
}

}  // namespace papyrix
