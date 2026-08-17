#pragma once

#include <functional>

class FsFile;
class Print;
class ZipFile;

class JpegToBmpConverter {
  static unsigned char jpegReadCallback(unsigned char* pBuf, unsigned char buf_size,
                                        unsigned char* pBytes_actually_read, void* pCallback_data);
  static bool jpegFileToBmpStreamInternal(class FsFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool requireDithering, const std::function<bool()>& shouldAbort);

 public:
  static bool jpegFileToBmpStream(FsFile& jpegFile, Print& bmpOut);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                          const std::function<bool()>& shouldAbort = nullptr);
  // Convert to 1-bit BMP (black and white only, no grays)
  static bool jpegFileTo1BitBmpStream(FsFile& jpegFile, Print& bmpOut);
  // Convert to 1-bit BMP with custom target size (for thumbnails)
  static bool jpegFileTo1BitBmpStreamWithSize(FsFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                              const std::function<bool()>& shouldAbort = nullptr,
                                              bool requireDithering = false);
};
