#include "ImageBlock.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <Serialization.h>

#define TAG "IMG_BLOCK"

void ImageBlock::render(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  auto renderPlaceholder = [&]() {
    const char* placeholder = "[Image]";
    const int textWidth = renderer.getTextWidth(fontId, placeholder);
    int textX = x + (static_cast<int>(width) - textWidth) / 2;
    if (textX < x) textX = x;
    const int textY = y + height / 2;
    renderer.drawText(fontId, textX, textY, placeholder, true);
  };

  if (cachedBmpPath.empty()) {
    renderPlaceholder();
    return;
  }

  FsFile bmpFile;
  if (!SdMan.openFileForRead("IMB", cachedBmpPath, bmpFile)) {
    LOG_ERR(TAG, "Failed to open cached BMP: %s", cachedBmpPath.c_str());
    renderPlaceholder();
    return;
  }

  Bitmap bitmap(bmpFile, true);
  const BmpReaderError err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_ERR(TAG, "BMP parse error: %s", Bitmap::errorToString(err));
    bmpFile.close();
    renderPlaceholder();
    return;
  }

  renderer.drawBitmap(bitmap, x, y, width, height);
  bmpFile.close();
}

bool ImageBlock::serialize(FsFile& file) const {
  serialization::writeString(file, cachedBmpPath);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  return true;
}

std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile& file) {
  std::string path;
  uint16_t w, h;

  if (!serialization::readString(file, path) || !serialization::readPodChecked(file, w) ||
      !serialization::readPodChecked(file, h)) {
    LOG_ERR(TAG, "Deserialization failed: couldn't read data");
    return nullptr;
  }

  // Sanity check: prevent unreasonable dimensions from corrupted data
  if (w > 2000 || h > 2000) {
    LOG_ERR(TAG, "Deserialization failed: dimensions %ux%u exceed maximum", w, h);
    return nullptr;
  }

  return std::unique_ptr<ImageBlock>(new ImageBlock(std::move(path), w, h));
}
