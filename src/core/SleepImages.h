#pragma once

#include <cstddef>

#include "Types.h"

class FsFile;

namespace papyrix::sleep_images {

constexpr std::size_t NAME_SIZE = BufferSize::Utf8Filename;
constexpr std::size_t PAGE_SIZE = 8;

struct Page {
  char names[PAGE_SIZE][NAME_SIZE]{};
  std::size_t offset = 0;
  std::size_t count = 0;
  std::size_t total = 1;
};

void loadPage(Page& page, std::size_t offset);
bool openImage(const char* name, FsFile& file);
bool openCustomImage(const char* preferred, FsFile& file);

}  // namespace papyrix::sleep_images
