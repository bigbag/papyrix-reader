#pragma once

#include <functional>

class FsFile;
class Print;

class PngToBmpConverter {
 public:
  static bool pngFileToBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                         bool oneBit = false, bool requireDithering = false,
                                         const std::function<bool()>& shouldAbort = nullptr);
};
