#pragma once

#include <SdFat.h>

#include <cstddef>
#include <cstdint>
#include <functional>

#include "Group5.h"

// G5 compressed image file header
// File format: [G5ImageHeader][compressed data]
struct G5ImageHeader {
  uint16_t magic;  // 0x4735 ('G5')
  uint16_t width;
  uint16_t height;
  uint32_t compressedSize;
};

constexpr uint16_t G5_MAGIC = 0x4735;  // 'G5' in little-endian

// Utility class for compressing/decompressing 1-bit images using Group5
class G5ImageCache {
 public:
  // Compress a 1-bit bitmap to a file
  // bitmap: MSB-first packed pixels (1 bit per pixel)
  // width/height: image dimensions in pixels
  // path: output file path
  // Returns true on success
  static bool compressToFile(const uint8_t* bitmap, int width, int height, const char* path);

  // Decompress a G5 file, calling rowCallback for each row
  // rowCallback receives: (rowData, rowBytes, y)
  // rowData: MSB-first packed pixels for this row
  // rowBytes: number of bytes in rowData
  // y: row index (0 to height-1)
  // Returns true on success
  static bool decompressFromFile(const char* path, std::function<void(const uint8_t*, int, int)> rowCallback);

  // Read header from a G5 file without decompressing
  // Returns true if valid G5 file, populating header
  static bool readHeader(const char* path, G5ImageHeader& header);

  // Estimate worst-case compressed size for given dimensions
  // Group5 can expand data in worst case, this provides safe buffer size
  static size_t estimateMaxCompressedSize(int width, int height);

 private:
  static constexpr size_t MAX_RAW_SIZE = 512 * 1024;
  static constexpr size_t MAX_COMPRESSED_SIZE = 512 * 1024;

  static bool validateHeader(const G5ImageHeader& header, size_t fileSize, size_t& rowBytes);
  static bool hasAllocationHeadroom(size_t bytes);
};
