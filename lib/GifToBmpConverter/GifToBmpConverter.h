#pragma once

#include <Print.h>

#include <functional>

class FsFile;

// Convert GIF files to BMP format using TinyGifDecoder
class GifToBmpConverter {
 public:
  // Convert GIF file to BMP stream
  static bool gifFileToBmpStream(FsFile& input, Print& output, int maxWidth = 480, int maxHeight = 800);

  // Convert with size constraints
  static bool gifFileToBmpStreamWithSize(FsFile& input, Print& output, int maxWidth, int maxHeight,
                                         std::function<bool()> shouldAbort = nullptr);

  // Quick mode: simple threshold instead of dithering
  static bool gifFileToBmpStreamQuick(FsFile& input, Print& output, int maxWidth, int maxHeight);
};