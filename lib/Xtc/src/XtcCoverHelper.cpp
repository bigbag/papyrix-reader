#include "XtcCoverHelper.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <SDCardManager.h>

#define TAG "XTC_COVER"

namespace xtc {

bool generateCoverBmpFromParser(XtcParser& parser, const std::string& coverBmpPath) {
  if (parser.getPageCount() == 0) {
    LOG_ERR(TAG, "No pages in XTC file");
    return false;
  }

  PageInfo pageInfo;
  if (!parser.getPageInfo(0, pageInfo)) {
    LOG_ERR(TAG, "Failed to get first page info");
    return false;
  }

  const uint8_t bitDepth = parser.getBitDepth();

  constexpr uint32_t MAX_DIMENSION = 2000;
  constexpr size_t MAX_BITMAP_SIZE = 512 * 1024;

  if (pageInfo.width == 0 || pageInfo.height == 0 || pageInfo.width > MAX_DIMENSION ||
      pageInfo.height > MAX_DIMENSION) {
    LOG_ERR(TAG, "Invalid dimensions: %ux%u", pageInfo.width, pageInfo.height);
    return false;
  }

  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
  } else {
    bitmapSize = (static_cast<size_t>(pageInfo.width + 7) / 8) * pageInfo.height;
  }

  if (bitmapSize > MAX_BITMAP_SIZE) {
    LOG_ERR(TAG, "Bitmap too large: %zu bytes", bitmapSize);
    return false;
  }

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    LOG_ERR(TAG, "Failed to allocate page buffer (%zu bytes)", bitmapSize);
    return false;
  }

  size_t bytesRead = parser.loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR(TAG, "Failed to load cover page");
    free(pageBuffer);
    return false;
  }

  FsFile coverBmp;
  if (!SdMan.openFileForWrite("XTC", coverBmpPath, coverBmp)) {
    LOG_ERR(TAG, "Failed to create cover BMP file");
    free(pageBuffer);
    return false;
  }

  // BMP header
  const uint32_t rowSize = ((pageInfo.width + 31) / 32) * 4;
  const uint32_t imageSize = rowSize * pageInfo.height;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;

  // File header
  coverBmp.write('B');
  coverBmp.write('M');
  coverBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  // DIB header (BITMAPINFOHEADER)
  uint32_t dibHeaderSize = 40;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t width = pageInfo.width;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&width), 4);
  int32_t height = -static_cast<int32_t>(pageInfo.height);  // Negative for top-down
  coverBmp.write(reinterpret_cast<const uint8_t*>(&height), 4);
  uint16_t planes = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  // Color palette (1-bit: black and white)
  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};
  coverBmp.write(black, 4);
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};
  coverBmp.write(white, 4);

  // Bitmap data
  const size_t dstRowSize = (pageInfo.width + 7) / 8;

  if (bitDepth == 2) {
    // XTH 2-bit: two bit planes, column-major, right-to-left
    const size_t planeSize = (static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8;
    const uint8_t* plane1 = pageBuffer;
    const uint8_t* plane2 = pageBuffer + planeSize;
    const size_t colBytes = (pageInfo.height + 7) / 8;

    uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(dstRowSize));
    if (!rowBuffer) {
      free(pageBuffer);
      coverBmp.close();
      return false;
    }

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      memset(rowBuffer, 0xFF, dstRowSize);

      for (uint16_t x = 0; x < pageInfo.width; x++) {
        const size_t colIndex = pageInfo.width - 1 - x;
        const size_t byteInCol = y / 8;
        const size_t bitInByte = 7 - (y % 8);

        const size_t byteOffset = colIndex * colBytes + byteInCol;
        const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
        const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
        const uint8_t pixelValue = (bit1 << 1) | bit2;

        if (pixelValue >= 1) {
          const size_t dstByte = x / 8;
          const size_t dstBit = 7 - (x % 8);
          rowBuffer[dstByte] &= ~(1 << dstBit);
        }
      }

      coverBmp.write(rowBuffer, dstRowSize);

      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - dstRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }

    free(rowBuffer);
  } else {
    // 1-bit source: write directly with padding
    const size_t srcRowSize = (pageInfo.width + 7) / 8;

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      coverBmp.write(pageBuffer + y * srcRowSize, srcRowSize);

      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - srcRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }
  }

  coverBmp.close();
  free(pageBuffer);

  LOG_INF(TAG, "Generated cover BMP: %s", coverBmpPath.c_str());
  return true;
}

}  // namespace xtc
