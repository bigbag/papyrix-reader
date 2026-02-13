#pragma once

#include <functional>
#include <Print.h>

class FsFile;

// Convert GIF files to BMP format
// Uses AnimatedGIF library to decode first frame only
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