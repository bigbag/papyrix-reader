#pragma once

class FsFile;
class Print;

class PngToBmpConverter {
 public:
  static bool pngFileToBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Quick preview mode: simple threshold instead of dithering (faster but lower quality)
  static bool pngFileToBmpStreamQuick(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
};
