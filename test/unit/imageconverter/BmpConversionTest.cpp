#include "test_utils.h"

#include <Bitmap.h>
#include <ImageConverter.h>
#include <SDCardManager.h>

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <string>

namespace {

void put16(std::string& data, const size_t offset, const uint16_t value) {
  memcpy(&data[offset], &value, 2);
}

void put32(std::string& data, const size_t offset, const uint32_t value) {
  memcpy(&data[offset], &value, 4);
}

void put32s(std::string& data, const size_t offset, const int32_t value) {
  memcpy(&data[offset], &value, 4);
}

// Horizontal gray gradient 24-bpp BMP. storage order follows `topDown`.
std::string build24bpp(const uint16_t w, const uint16_t h, const bool topDown) {
  const uint32_t rowSize = (static_cast<uint32_t>(w) * 3 + 3) / 4 * 4;
  std::string data(14 + 40 + rowSize * h, '\0');
  data[0] = 'B';
  data[1] = 'M';
  put32(data, 2, static_cast<uint32_t>(data.size()));
  put32(data, 10, 54);
  put32(data, 14, 40);
  put32s(data, 18, w);
  put32s(data, 22, topDown ? static_cast<int32_t>(h) : -static_cast<int32_t>(h));
  put16(data, 26, 1);
  put16(data, 28, 24);
  put32(data, 34, rowSize * h);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t y = topDown ? row : static_cast<uint16_t>(h - 1 - row);
    for (uint16_t x = 0; x < w; x++) {
      const uint8_t v = static_cast<uint8_t>((x * 255) / (w > 1 ? w - 1 : 1));
      const size_t off = 54 + static_cast<size_t>(row) * rowSize + static_cast<size_t>(x) * 3;
      data[off] = v;
      data[off + 1] = v;
      data[off + 2] = v;
    }
  }
  return data;
}

// 8-bpp paletted BMP, dark palette entry 0 and white entry 1, checkerboard.
std::string build8bpp(const uint16_t w, const uint16_t h) {
  const uint32_t rowSize = (static_cast<uint32_t>(w) + 3) / 4 * 4;
  const size_t paletteOffset = 54;
  const size_t dataOffset = paletteOffset + 1024;
  std::string data(dataOffset + rowSize * h, '\0');
  data[0] = 'B';
  data[1] = 'M';
  put32(data, 2, static_cast<uint32_t>(data.size()));
  put32(data, 10, static_cast<uint32_t>(dataOffset));
  put32(data, 14, 40);
  put32s(data, 18, w);
  put32s(data, 22, -static_cast<int32_t>(h));  // bottom-up
  put16(data, 26, 1);
  put16(data, 28, 8);
  put32(data, 34, rowSize * h);
  put32(data, 46, 2);
  // palette entries: BGRX
  memset(&data[paletteOffset], 0x00, 4);
  memset(&data[paletteOffset + 4], 0xFF, 3);
  data[paletteOffset + 7] = 0x00;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t y = static_cast<uint16_t>(h - 1 - row);
    for (uint16_t x = 0; x < w; x++) {
      data[dataOffset + static_cast<size_t>(row) * rowSize + x] = ((x ^ y) & 1) ? 1 : 0;
    }
  }
  return data;
}

// 1-bpp top-down BMP (already canonical). Rows padded to 4 bytes.
std::string build1bpp(const uint16_t w, const uint16_t h) {
  const uint32_t rowSize = ((static_cast<uint32_t>(w) + 31) / 32) * 4;
  const size_t dataOffset = 54 + 8;
  std::string data(dataOffset + rowSize * h, '\0');
  data[0] = 'B';
  data[1] = 'M';
  put32(data, 2, static_cast<uint32_t>(data.size()));
  put32(data, 10, static_cast<uint32_t>(dataOffset));
  put32(data, 14, 40);
  put32s(data, 18, w);
  put32s(data, 22, -static_cast<int32_t>(h));  // negative height = top-down
  put16(data, 26, 1);
  put16(data, 28, 1);
  put32(data, 34, rowSize * h);
  put32(data, 38, 2835);
  put32(data, 42, 2835);
  put32(data, 46, 2);
  put32(data, 50, 2);
  // palette: entry 0 black, entry 1 white
  memset(&data[54 + 4], 0xFF, 3);
  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      if ((x ^ y) & 1) data[dataOffset + y * rowSize + x / 8] |= static_cast<char>(1u << (7 - (x % 8)));
    }
  }
  return data;
}

bool convert(const std::string& source, std::string& out, const int maxW = 0, const int maxH = 0,
             const bool oneBit = true) {
  SdMan.clearFiles();
  SdMan.clearWrittenFiles();
  SdMan.registerFile("/in.bmp", source);

  ImageConvertConfig config;
  config.maxWidth = maxW;
  config.maxHeight = maxH;
  config.oneBit = oneBit;
  const bool ok = ImageConverterFactory::convertToBmp("/in.bmp", "/out.bmp", config);
  out = SdMan.getWrittenData("/out.bmp");
  return ok;
}

bool outputIsCanonical1Bit(const std::string& out, const int width, const int height) {
  if (out.size() < 62) return false;
  FsFile file;
  SdMan.clearFiles();
  SdMan.registerFile("/check.bmp", out);
  if (!SdMan.openFileForRead("CHK", "/check.bmp", file)) return false;
  Bitmap bitmap(file);
  const bool valid = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.hasCompletePixelData() &&
                     bitmap.getBpp() == 1 && bitmap.isTopDown() && bitmap.getWidth() == width &&
                     bitmap.getHeight() == height;
  file.close();
  return valid;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("BmpConversionTest");

  // ---- Test: 24-bpp bottom-up converts to canonical 1-bit top-down ----
  {
    std::string out;
    runner.expectTrue(convert(build24bpp(32, 16, false), out), "24bpp bottom-up: conversion succeeds");
    runner.expectTrue(outputIsCanonical1Bit(out, 32, 16), "24bpp bottom-up: canonical 1-bit top-down output");
  }

  // ---- Test: orientation is normalized (bottom-up == top-down output) ----
  {
    std::string outTop, outBottom;
    runner.expectTrue(convert(build24bpp(32, 16, true), outTop), "orientation: top-down converts");
    runner.expectTrue(convert(build24bpp(32, 16, false), outBottom), "orientation: bottom-up converts");
    runner.expectTrue(outTop == outBottom, "orientation: both orders produce identical output");
  }

  // ---- Test: 1-bpp top-down input passes through unchanged ----
  {
    const std::string source = build1bpp(24, 10);
    std::string out;
    runner.expectTrue(convert(source, out), "1bpp passthrough: conversion succeeds");
    runner.expectTrue(out == source, "1bpp passthrough: bytes unchanged");
  }

  // ---- Test: 8-bpp palette converts ----
  {
    std::string out;
    runner.expectTrue(convert(build8bpp(16, 8), out), "8bpp: conversion succeeds");
    runner.expectTrue(outputIsCanonical1Bit(out, 16, 8), "8bpp: canonical output");
  }

  // ---- Test: downscaling honors max dimensions ----
  {
    std::string out;
    runner.expectTrue(convert(build24bpp(100, 50, true), out, 50, 25), "scale: conversion succeeds");
    runner.expectTrue(outputIsCanonical1Bit(out, 50, 25), "scale: output is 50x25");
  }

  // ---- Test: cancellation fails without publishing ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.registerFile("/in.bmp", build24bpp(32, 16, true));
    ImageConvertConfig config;
    config.maxWidth = 0;
    config.maxHeight = 0;
    config.oneBit = true;
    config.shouldAbort = []() { return true; };
    runner.expectFalse(ImageConverterFactory::convertToBmp("/in.bmp", "/out.bmp", config),
                       "cancel: conversion fails");
    runner.expectFalse(SdMan.exists("/out.bmp"), "cancel: nothing published");
    runner.expectFalse(SdMan.exists("/out.bmp.part"), "cancel: no part left");
  }

  // ---- Test: 2-bit output path ----
  {
    std::string out;
    runner.expectTrue(convert(build24bpp(32, 16, true), out, 0, 0, false), "2bit: conversion succeeds");
    runner.expectTrue(out.size() >= 70 && static_cast<uint8_t>(out[28]) == 2, "2bit: output is 2-bpp BMP");
  }

  return runner.allPassed() ? 0 : 1;
}
