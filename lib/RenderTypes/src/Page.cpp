#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

#define TAG "PAGE"

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset, const bool black) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset, black);
}

bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

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
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  return block->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto ib = ImageBlock::deserialize(file);
  if (!ib) {
    return nullptr;
  }
  return std::unique_ptr<PageImage>(new PageImage(std::move(ib), xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                  const bool black) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset, black);
  }
}

bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));
    if (!el->serialize(file)) {
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  // Max elements per page - prevents memory exhaustion from corrupted cache
  constexpr uint16_t MAX_PAGE_ELEMENTS = 500;

  uint16_t count;
  serialization::readPod(file, count);

  // Validate element count to prevent memory exhaustion
  if (count > MAX_PAGE_ELEMENTS) {
    LOG_ERR(TAG, "Element count %u exceeds limit %u", count, MAX_PAGE_ELEMENTS);
    return nullptr;
  }

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

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
