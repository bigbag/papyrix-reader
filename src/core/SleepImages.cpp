#include "SleepImages.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <FsHelpers.h>
#include <SDCardManager.h>

#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_random.h>
#endif

namespace papyrix::sleep_images {
namespace {

bool validName(const char* name) {
  return name && name[0] && name[0] != '.' && strlen(name) < NAME_SIZE && !strchr(name, '/') && !strchr(name, '\\') &&
         FsHelpers::isBmpFile(name);
}

bool validBitmap(FsFile& file) {
  Bitmap bitmap(file);
  return !file.isDirectory() && bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.hasCompletePixelData();
}

bool openValidated(const char* path, FsFile& file) {
  file.close();
  if (!SdMan.openFileForRead("SLP", path, file)) {
    file.close();
    return false;
  }
  if (!validBitmap(file) || !file.seek(0)) {
    file.close();
    return false;
  }
  return true;
}

template <typename Visitor>
void scan(Visitor visit) {
  auto dir = SdMan.open("/sleep");
  if (!dir || !dir.isDirectory()) {
    dir.close();
    return;
  }
  uint32_t entries = 0;
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if ((++entries & 0xFFu) == 0) delay(1);
    char name[NAME_SIZE] = {};
    entry.getName(name, sizeof(name));
    const bool valid = validName(name) && validBitmap(entry);
    entry.close();
    if (valid) visit(name);
  }
  dir.close();
}

size_t randomBelow(size_t bound) {
  const size_t threshold = -bound % bound;
  size_t value;
  do {
    value = 0;
    for (size_t bit = 0; bit < sizeof(value) * 8; bit += 32) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
      const uint32_t sample = esp_random();
#else
      const uint32_t sample = (static_cast<uint32_t>(std::rand()) << 16) ^ static_cast<uint32_t>(std::rand());
#endif
      if (bit) value = (value << 16) << 16;
      value |= sample;
    }
  } while (value < threshold);
  return value % bound;
}

}  // namespace

bool openImage(const char* name, FsFile& file) {
  if (!validName(name)) {
    file.close();
    return false;
  }
  char path[sizeof("/sleep/") + NAME_SIZE - 1];
  memcpy(path, "/sleep/", sizeof("/sleep/") - 1);
  strcpy(path + sizeof("/sleep/") - 1, name);
  return openValidated(path, file);
}

void loadPage(Page& page, size_t offset) {
  offset -= offset % PAGE_SIZE;
  auto fill = [&](size_t start) {
    page.offset = start;
    page.count = 0;
    page.total = 1;
    if (start == 0) {
      page.names[0][0] = '\0';
      ++page.count;
    }
    scan([&](const char* name) {
      const size_t index = page.total++;
      if (index >= start && index - start < PAGE_SIZE) {
        strcpy(page.names[page.count++], name);
      }
    });
  };
  fill(offset);
  if (offset >= page.total) {
    const size_t last = ((page.total - 1) / PAGE_SIZE) * PAGE_SIZE;
    fill(last);
  }
}

bool openCustomImage(const char* preferred, FsFile& file) {
  if (openImage(preferred, file)) return true;
  size_t count = 0;
  char name[NAME_SIZE] = {};
  scan([&](const char* candidate) {
    if (randomBelow(++count) == 0) strcpy(name, candidate);
  });
  if (name[0] && openImage(name, file)) return true;
  return openValidated("/sleep.bmp", file);
}

}  // namespace papyrix::sleep_images
