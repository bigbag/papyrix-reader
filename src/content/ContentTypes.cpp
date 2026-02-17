#include "ContentTypes.h"

#include <cstring>

namespace papyrix {

ContentType detectContentType(const char* path) {
  if (!path) return ContentType::None;

  const char* ext = strrchr(path, '.');
  if (!ext) return ContentType::None;

  // Case-insensitive extension check
  if (strcasecmp(ext, ".epub") == 0) {
    return ContentType::Epub;
  }
  if (strcasecmp(ext, ".xtc") == 0 || strcasecmp(ext, ".xtch") == 0 || strcasecmp(ext, ".xtg") == 0 ||
      strcasecmp(ext, ".xth") == 0) {
    return ContentType::Xtc;
  }
  if (strcasecmp(ext, ".txt") == 0) {
    return ContentType::Txt;
  }
  if (strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".markdown") == 0) {
    return ContentType::Markdown;
  }
  if (strcasecmp(ext, ".fb2") == 0) {
    return ContentType::Fb2;
  }

  return ContentType::None;
}

}  // namespace papyrix
