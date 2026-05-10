#include "Bitmap.h"

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
  if (colorsUsed > 256u) return BmpReaderError::PaletteTooLarge;
  if (width <= 0 || height <= 0) return BmpReaderError::BadDimensions;

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (width > MAX_IMAGE_WIDTH || height > MAX_IMAGE_HEIGHT) {
    return BmpReaderError::ImageTooLarge;
  }

  rowBytes = (width * bpp + 31) / 32 * 4;

  for (int i = 0; i < 256; i++) paletteLum[i] = static_cast<uint8_t>(i);
  if (colorsUsed > 0) {
    uint8_t palBuf[256 * 4];
    const int palBytes = static_cast<int>(colorsUsed * 4);
    if (file.read(palBuf, palBytes) != palBytes) return BmpReaderError::FileInvalid;
    for (uint32_t i = 0; i < colorsUsed; i++) {
      const uint8_t* rgb = palBuf + i * 4;
      paletteLum[i] = (77u * rgb[2] + 150u * rgb[1] + 29u * rgb[0]) >> 8;
    }
  }

  isIdentityPalette_ = (bpp == 2 && colorsUsed >= 4 && paletteLum[0] == 0x00 && paletteLum[1] == 0x55 &&
                        paletteLum[2] == 0xAA && paletteLum[3] == 0xFF);

  if (!file.seek(bfOffBits)) {
    return BmpReaderError::SeekPixelDataFailed;
  }

  delete atkinsonDitherer;
  atkinsonDitherer = nullptr;
  delete fsDitherer;
  fsDitherer = nullptr;

  if (bpp > 2 && dithering) {
    if (USE_ATKINSON) {
      atkinsonDitherer = new AtkinsonDitherer(width);
    } else {
      fsDitherer = new FloydSteinbergDitherer(width);
    }
  }

  return BmpReaderError::Ok;
}

// packed 2bpp output, 0 = black, 1 = dark gray, 2 = light gray, 3 = white
BmpReaderError Bitmap::readRow(uint8_t* data, uint8_t* rowBuffer, int rowY) const {
  if (preloadedRows_) {
    const uint8_t* src = preloadedRow(rowY);
    if (!src) return BmpReaderError::ShortReadRow;
    memcpy(rowBuffer, src, rowBytes);
  } else {
    if (file.read(rowBuffer, rowBytes) != rowBytes) return BmpReaderError::ShortReadRow;
  }

  prevRowY += 1;

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

bool Bitmap::preloadAllRows() const {
  if (preloadedRows_) return true;
  if (rowBytes <= 0 || height <= 0) return false;

  const size_t total = static_cast<size_t>(rowBytes) * static_cast<size_t>(height);
  if (total > 256 * 1024) return false;

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
