#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define TAG "PAGE"

IRAM_ATTR void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                                const bool black) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset, black);
}

bool PageLine::serialize(FsFile& file) {
  return serialization::writePodChecked(file, xPos) && serialization::writePodChecked(file, yPos) &&
         block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  if (!serialization::readPodChecked(file, xPos) || !serialization::readPodChecked(file, yPos)) {
    return nullptr;
  }

  auto tb = TextBlock::deserialize(file);
  if (!tb) {
    LOG_ERR(TAG, "Deserialization failed: TextBlock is null");
    return nullptr;
  }
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                       const bool black) {
  if (!black) {
    renderer.clearArea(xPos + xOffset, yPos + yOffset, block->getWidth(), block->getHeight(), 0xFF);
  }
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(FsFile& file) {
  return serialization::writePodChecked(file, xPos) && serialization::writePodChecked(file, yPos) &&
         block->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  if (!serialization::readPodChecked(file, xPos) || !serialization::readPodChecked(file, yPos)) {
    return nullptr;
  }

  auto ib = ImageBlock::deserialize(file);
  if (!ib) {
    return nullptr;
  }
  return std::unique_ptr<PageImage>(new PageImage(std::move(ib), xPos, yPos));
}

IRAM_ATTR void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                            const bool black) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset, black);
  }
}

void Page::warmGlyphs(const GfxRenderer& renderer, const int fontId) const {
  std::vector<uint32_t> bucket[4];
  for (auto& v : bucket) v.reserve(64);

  for (const auto& element : elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& tb = static_cast<const PageLine&>(*element).getTextBlock();
    for (const auto& wd : tb.getWords()) {
      const int idx = static_cast<int>(wd.style);
      if (idx < 0 || idx >= 4) continue;
      const unsigned char* ptr = reinterpret_cast<const unsigned char*>(wd.word.c_str());
      uint32_t cp;
      while ((cp = utf8NextCodepoint(&ptr))) {
        bucket[idx].push_back(cp);
      }
    }
  }

  for (int s = 0; s < 4; s++) {
    auto& b = bucket[s];
    if (b.empty()) continue;
    std::sort(b.begin(), b.end());
    b.erase(std::unique(b.begin(), b.end()), b.end());
    renderer.warmCodepointsBatch(fontId, b.data(), b.size(), static_cast<EpdFontFamily::Style>(s));
  }
}

bool Page::serialize(FsFile& file) const {
  if (elements.size() > UINT16_MAX) return false;
  const uint16_t count = static_cast<uint16_t>(elements.size());
  if (!serialization::writePodChecked(file, count)) return false;

  for (const auto& el : elements) {
    if (!serialization::writePodChecked(file, static_cast<uint8_t>(el->getTag())) || !el->serialize(file)) {
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  // Max elements per page - prevents memory exhaustion from corrupted cache
  constexpr uint16_t MAX_PAGE_ELEMENTS = 500;

  uint16_t count = 0;
  if (!serialization::readPodChecked(file, count)) {
    LOG_ERR(TAG, "Deserialization failed: couldn't read element count");
    return nullptr;
  }

  // Validate element count to prevent memory exhaustion
  if (count > MAX_PAGE_ELEMENTS) {
    LOG_ERR(TAG, "Element count %u exceeds limit %u", count, MAX_PAGE_ELEMENTS);
    return nullptr;
  }

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag = 0;
    if (!serialization::readPodChecked(file, tag)) {
      LOG_ERR(TAG, "Deserialization failed: couldn't read element tag");
      return nullptr;
    }

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        LOG_ERR(TAG, "Deserialization failed: PageLine is null");
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (!pi) {
        LOG_ERR(TAG, "Deserialization failed: PageImage is null");
        return nullptr;
      }
      page->elements.push_back(std::move(pi));
    } else {
      LOG_ERR(TAG, "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  return page;
}
