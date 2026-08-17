#include "Bitmap.h"

#include <esp_heap_caps.h>

#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================================
// IMAGE PROCESSING OPTIONS - Toggle these to test different configurations
// ============================================================================
// Note: For cover images, dithering is done in JpegToBmpConverter.cpp
// This file handles BMP reading - use simple quantization to avoid double-dithering
constexpr bool USE_ATKINSON = true;  // Use Atkinson dithering instead of Floyd-Steinberg
// ============================================================================

Bitmap::~Bitmap() {
  delete[] preloadedRows_;
  delete atkinsonDitherer;
  delete fsDitherer;
}

const char* Bitmap::errorToString(BmpReaderError err) {
  switch (err) {
    case BmpReaderError::Ok:
      return "Ok";
    case BmpReaderError::FileInvalid:
      return "FileInvalid";
    case BmpReaderError::SeekStartFailed:
      return "SeekStartFailed";
    case BmpReaderError::NotBMP:
      return "NotBMP (missing 'BM')";
    case BmpReaderError::DIBTooSmall:
      return "DIBTooSmall (<40 bytes)";
    case BmpReaderError::BadPlanes:
      return "BadPlanes (!= 1)";
    case BmpReaderError::UnsupportedBpp:
      return "UnsupportedBpp (expected 1, 2, 8, 24, or 32)";
    case BmpReaderError::UnsupportedCompression:
      return "UnsupportedCompression (expected BI_RGB or BI_BITFIELDS for 32bpp)";
    case BmpReaderError::BadDimensions:
      return "BadDimensions";
    case BmpReaderError::ImageTooLarge:
      return "ImageTooLarge (max 2048x3072)";
    case BmpReaderError::PaletteTooLarge:
      return "PaletteTooLarge";

    case BmpReaderError::SeekPixelDataFailed:
      return "SeekPixelDataFailed";
    case BmpReaderError::BufferTooSmall:
      return "BufferTooSmall";

    case BmpReaderError::OomRowBuffer:
      return "OomRowBuffer";
    case BmpReaderError::ShortReadRow:
      return "ShortReadRow";
  }
  return "Unknown";
}

BmpReaderError Bitmap::parseHeaders() {
  if (!file) return BmpReaderError::FileInvalid;
  if (!file.seek(0)) return BmpReaderError::SeekStartFailed;

  uint8_t hdr[54];
  if (file.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) {
    return BmpReaderError::FileInvalid;
  }

  auto leU16 = [](const uint8_t* p) -> uint16_t { return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8)); };
  auto leU32 = [](const uint8_t* p) -> uint32_t {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  };

  if (leU16(hdr + 0) != 0x4D42) return BmpReaderError::NotBMP;
  bfOffBits = leU32(hdr + 10);
  const uint32_t biSize = leU32(hdr + 14);
  if (biSize < 40) return BmpReaderError::DIBTooSmall;

  width = static_cast<int32_t>(leU32(hdr + 18));
  const auto rawHeight = static_cast<int32_t>(leU32(hdr + 22));
  topDown = rawHeight < 0;
  height = topDown ? -rawHeight : rawHeight;

  const uint16_t planes = leU16(hdr + 26);
  bpp = leU16(hdr + 28);
  const uint32_t comp = leU32(hdr + 30);
  const uint32_t colorsUsed = leU32(hdr + 46);
  const bool validBpp = bpp == 1 || bpp == 2 || bpp == 8 || bpp == 24 || bpp == 32;

  if (planes != 1) return BmpReaderError::BadPlanes;
  if (!validBpp) return BmpReaderError::UnsupportedBpp;
  if (!(comp == 0 || (bpp == 32 && comp == 3))) return BmpReaderError::UnsupportedCompression;
  const uint32_t paletteEntries = colorsUsed == 0 && bpp <= 8 ? 1U << bpp : colorsUsed;
  if (paletteEntries > 256u) return BmpReaderError::PaletteTooLarge;
  if (width <= 0 || height <= 0) return BmpReaderError::BadDimensions;

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (width > MAX_IMAGE_WIDTH || height > MAX_IMAGE_HEIGHT) {
    return BmpReaderError::ImageTooLarge;
  }

  rowBytes = (width * bpp + 31) / 32 * 4;

  for (int i = 0; i < 256; i++) paletteLum[i] = static_cast<uint8_t>(i);
  if (bpp == 1) {
    paletteLum[0] = 0;
    paletteLum[1] = 255;
  } else if (bpp == 2) {
    paletteLum[0] = 0;
    paletteLum[1] = 85;
    paletteLum[2] = 170;
    paletteLum[3] = 255;
  }
  if (paletteEntries > 0) {
    const uint64_t paletteOffset = 14U + static_cast<uint64_t>(biSize);
    const uint64_t paletteSize = static_cast<uint64_t>(paletteEntries) * 4U;
    if (paletteOffset > bfOffBits || paletteSize > static_cast<uint64_t>(bfOffBits) - paletteOffset ||
        !file.seek(paletteOffset)) {
      return BmpReaderError::FileInvalid;
    }
    for (uint32_t i = 0; i < paletteEntries; i++) {
      uint8_t rgb[4];
      if (file.read(rgb, sizeof(rgb)) != sizeof(rgb)) return BmpReaderError::FileInvalid;
      paletteLum[i] = (77u * rgb[2] + 150u * rgb[1] + 29u * rgb[0]) >> 8;
    }
  }

  isIdentityPalette_ = (bpp == 2 && paletteEntries >= 4 && paletteLum[0] == 0x00 && paletteLum[1] == 0x55 &&
                        paletteLum[2] == 0xAA && paletteLum[3] == 0xFF);

  if (!file.seek(bfOffBits)) {
    return BmpReaderError::SeekPixelDataFailed;
  }

  delete atkinsonDitherer;
  atkinsonDitherer = nullptr;
  delete fsDitherer;
  fsDitherer = nullptr;

  if (bpp > 2 && dithering) {
    // OOM-safe: fall back to non-dithered output if row buffers can't be allocated.
    if (USE_ATKINSON) {
      atkinsonDitherer = new (std::nothrow) AtkinsonDitherer(width);
      if (atkinsonDitherer && !atkinsonDitherer->valid()) {
        delete atkinsonDitherer;
        atkinsonDitherer = nullptr;
      }
    } else {
      fsDitherer = new (std::nothrow) FloydSteinbergDitherer(width);
      if (fsDitherer && !fsDitherer->valid()) {
        delete fsDitherer;
        fsDitherer = nullptr;
      }
    }
  }

  return BmpReaderError::Ok;
}

BmpReaderError Bitmap::loadRawRow(uint8_t* rowBuffer, const int storageRowY) const {
  if (!rowBuffer || storageRowY < 0 || storageRowY >= height) return BmpReaderError::ShortReadRow;

  if (preloadedRows_) {
    const uint8_t* source = preloadedRow(storageRowY);
    if (!source) return BmpReaderError::ShortReadRow;
    memcpy(rowBuffer, source, rowBytes);
  } else {
    if (storageRowY != prevRowY + 1) {
      const uint64_t offset =
          static_cast<uint64_t>(bfOffBits) + static_cast<uint64_t>(storageRowY) * static_cast<uint64_t>(rowBytes);
      if (offset > UINT32_MAX || !file.seek(static_cast<uint32_t>(offset))) return BmpReaderError::ShortReadRow;
    }
    if (file.read(rowBuffer, rowBytes) != rowBytes) return BmpReaderError::ShortReadRow;
  }

  prevRowY = storageRowY;
  return BmpReaderError::Ok;
}

// packed 2bpp output, 0 = black, 1 = dark gray, 2 = light gray, 3 = white
BmpReaderError Bitmap::readRow(uint8_t* data, uint8_t* rowBuffer, int rowY) const {
  const BmpReaderError loadResult = loadRawRow(rowBuffer, rowY);
  if (loadResult != BmpReaderError::Ok) return loadResult;

  uint8_t* outPtr = data;
  uint8_t currentOutByte = 0;
  int bitShift = 6;
  int currentX = 0;

  // Helper lambda to pack 2bpp color into the output stream
  auto packPixel = [&](const uint8_t lum) {
    uint8_t color;
    if (atkinsonDitherer) {
      color = atkinsonDitherer->processPixel(adjustPixel(lum), currentX);
    } else if (fsDitherer) {
      color = fsDitherer->processPixel(adjustPixel(lum), currentX);
    } else {
      if (bpp > 2) {
        // Simple quantization or noise dithering
        color = quantize(adjustPixel(lum), currentX, prevRowY);
      } else {
        // do not quantize 2bpp image
        color = static_cast<uint8_t>(lum >> 6);
      }
    }
    currentOutByte |= (color << bitShift);
    if (bitShift == 0) {
      *outPtr++ = currentOutByte;
      currentOutByte = 0;
      bitShift = 6;
    } else {
      bitShift -= 2;
    }
    currentX++;
  };

  uint8_t lum;

  switch (bpp) {
    case 32: {
      const uint8_t* p = rowBuffer;
      for (int x = 0; x < width; x++) {
        lum = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
        packPixel(lum);
        p += 4;
      }
      break;
    }
    case 24: {
      const uint8_t* p = rowBuffer;
      for (int x = 0; x < width; x++) {
        lum = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
        packPixel(lum);
        p += 3;
      }
      break;
    }
    case 8: {
      for (int x = 0; x < width; x++) {
        packPixel(paletteLum[rowBuffer[x]]);
      }
      break;
    }
    case 2: {
      if (isIdentityPalette_ && !atkinsonDitherer && !fsDitherer) {
        const int bytesIn = (width * 2 + 7) / 8;
        memcpy(data, rowBuffer, bytesIn);
        return BmpReaderError::Ok;
      }
      for (int x = 0; x < width; x++) {
        lum = paletteLum[(rowBuffer[x >> 2] >> (6 - ((x & 3) << 1))) & 0x03];
        packPixel(lum);
      }
      break;
    }
    case 1: {
      for (int x = 0; x < width; x++) {
        lum = (rowBuffer[x >> 3] & (0x80 >> (x & 7))) ? 0xFF : 0x00;
        packPixel(lum);
      }
      break;
    }
    default:
      return BmpReaderError::UnsupportedBpp;
  }

  if (atkinsonDitherer)
    atkinsonDitherer->nextRow();
  else if (fsDitherer)
    fsDitherer->nextRow();

  // Flush remaining bits if width is not a multiple of 4
  if (bitShift != 6) *outPtr = currentOutByte;

  return BmpReaderError::Ok;
}

BmpReaderError Bitmap::readRawRow(uint8_t* rowBuffer, const size_t rowBufferSize, const int logicalRowY) const {
  if (!rowBuffer || rowBufferSize < static_cast<size_t>(rowBytes) || logicalRowY < 0 || logicalRowY >= height) {
    return BmpReaderError::BufferTooSmall;
  }

  const int storageRowY = topDown ? logicalRowY : height - 1 - logicalRowY;
  return loadRawRow(rowBuffer, storageRowY);
}

BmpReaderError Bitmap::readGrayscaleRow(uint8_t* gray, const size_t graySize, uint8_t* rowBuffer,
                                        const size_t rowBufferSize, const int logicalRowY) const {
  if (!gray || graySize < static_cast<size_t>(width) || !rowBuffer || rowBufferSize < static_cast<size_t>(rowBytes) ||
      logicalRowY < 0 || logicalRowY >= height) {
    return BmpReaderError::BufferTooSmall;
  }

  const int storageRowY = topDown ? logicalRowY : height - 1 - logicalRowY;
  const BmpReaderError loadResult = loadRawRow(rowBuffer, storageRowY);
  if (loadResult != BmpReaderError::Ok) return loadResult;

  switch (bpp) {
    case 32:
      for (int x = 0; x < width; ++x) {
        const uint8_t* pixel = rowBuffer + static_cast<size_t>(x) * 4;
        gray[x] = static_cast<uint8_t>((77U * pixel[2] + 150U * pixel[1] + 29U * pixel[0]) >> 8);
      }
      break;
    case 24:
      for (int x = 0; x < width; ++x) {
        const uint8_t* pixel = rowBuffer + static_cast<size_t>(x) * 3;
        gray[x] = static_cast<uint8_t>((77U * pixel[2] + 150U * pixel[1] + 29U * pixel[0]) >> 8);
      }
      break;
    case 8:
      for (int x = 0; x < width; ++x) gray[x] = paletteLum[rowBuffer[x]];
      break;
    case 2:
      for (int x = 0; x < width; ++x) {
        const uint8_t index = (rowBuffer[x >> 2] >> (6 - ((x & 3) << 1))) & 0x03;
        gray[x] = paletteLum[index];
      }
      break;
    case 1:
      for (int x = 0; x < width; ++x) {
        const uint8_t index = (rowBuffer[x >> 3] >> (7 - (x & 7))) & 0x01;
        gray[x] = paletteLum[index];
      }
      break;
    default:
      return BmpReaderError::UnsupportedBpp;
  }

  return BmpReaderError::Ok;
}

bool Bitmap::hasCompletePixelData() const {
  if (width <= 0 || height <= 0 || rowBytes <= 0) return false;
  const uint64_t required =
      static_cast<uint64_t>(bfOffBits) + static_cast<uint64_t>(rowBytes) * static_cast<uint64_t>(height);
  return required <= static_cast<uint64_t>(file.size());
}

bool Bitmap::preloadAllRows() const {
  if (preloadedRows_) return true;
  if (rowBytes <= 0 || height <= 0) return false;

  if (static_cast<size_t>(rowBytes) > SIZE_MAX / static_cast<size_t>(height)) return false;
  const size_t total = static_cast<size_t>(rowBytes) * static_cast<size_t>(height);
  if (total > 256 * 1024) return false;
  if (total > 1024 && total > heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) * 80 / 100) return false;

  preloadedRows_ = new (std::nothrow) uint8_t[total];
  if (!preloadedRows_) return false;

  if (!file.seek(bfOffBits)) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    return false;
  }
  if (file.read(preloadedRows_, total) != static_cast<int>(total)) {
    delete[] preloadedRows_;
    preloadedRows_ = nullptr;
    file.seek(bfOffBits);
    return false;
  }
  return true;
}

const uint8_t* Bitmap::preloadedRow(int rowIndex) const {
  if (!preloadedRows_ || rowIndex < 0 || rowIndex >= height) return nullptr;
  return preloadedRows_ + static_cast<size_t>(rowIndex) * static_cast<size_t>(rowBytes);
}

BmpReaderError Bitmap::rewindToData() const {
  if (!preloadedRows_) {
    if (!file.seek(bfOffBits)) {
      return BmpReaderError::SeekPixelDataFailed;
    }
  }

  prevRowY = -1;
  if (fsDitherer) fsDitherer->reset();
  if (atkinsonDitherer) atkinsonDitherer->reset();

  return BmpReaderError::Ok;
}
