#pragma once

#include <SdFat.h>

#include <cstdint>

#include "BitmapHelpers.h"

enum class BmpReaderError : uint8_t {
  Ok = 0,
  FileInvalid,
  SeekStartFailed,

  NotBMP,
  DIBTooSmall,

  BadPlanes,
  UnsupportedBpp,
  UnsupportedCompression,

  BadDimensions,
  ImageTooLarge,
  PaletteTooLarge,

  SeekPixelDataFailed,
  BufferTooSmall,
  OomRowBuffer,
  ShortReadRow,
};

class Bitmap {
 public:
  static const char* errorToString(BmpReaderError err);

  explicit Bitmap(FsFile& file, bool dithering = false) : file(file), dithering(dithering) {}
  ~Bitmap();
  BmpReaderError parseHeaders();
  BmpReaderError readRow(uint8_t* data, uint8_t* rowBuffer, int rowY) const;
  BmpReaderError rewindToData() const;
  bool preloadAllRows() const;
  bool isPreloaded() const { return preloadedRows_ != nullptr; }
  const uint8_t* preloadedRow(int rowIndex) const;
  int getWidth() const { return width; }
  int getHeight() const { return height; }
  bool isTopDown() const { return topDown; }
  bool hasGreyscale() const { return bpp > 1; }
  uint16_t getBpp() const { return bpp; }
  int getRowBytes() const { return rowBytes; }
  bool isIdentityPalette() const { return isIdentityPalette_; }

 private:
  FsFile& file;
  bool dithering = false;
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t bfOffBits = 0;
  uint16_t bpp = 0;
  int rowBytes = 0;
  uint8_t paletteLum[256] = {};
  bool isIdentityPalette_ = false;
  mutable uint8_t* preloadedRows_ = nullptr;

  // Dithering state (mutable for const methods)
  mutable int prevRowY = -1;
  mutable AtkinsonDitherer* atkinsonDitherer = nullptr;
  mutable FloydSteinbergDitherer* fsDitherer = nullptr;
};
