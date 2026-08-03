#include "G5ImageCache.h"

#include <SDCardManager.h>
#include <esp_heap_caps.h>

bool G5ImageCache::compressToFile(const uint8_t* bitmap, int width, int height, const char* path) {
  if (!bitmap || width <= 0 || height <= 0 || !path) {
    return false;
  }

  if (width > UINT16_MAX || height > UINT16_MAX) {
    return false;
  }

  const size_t rowBytes = (static_cast<size_t>(width) + 7) / 8;
  if (static_cast<size_t>(height) > MAX_RAW_SIZE / rowBytes) {
    return false;
  }

  const size_t maxCompressedSize = estimateMaxCompressedSize(width, height);
  if (maxCompressedSize > MAX_COMPRESSED_SIZE || !hasAllocationHeadroom(maxCompressedSize)) {
    return false;
  }

  uint8_t* compressBuffer = new (std::nothrow) uint8_t[maxCompressedSize];
  if (!compressBuffer) {
    return false;
  }

  G5ENCODER encoder;
  int result = encoder.init(width, height, compressBuffer, maxCompressedSize);
  if (result != G5_SUCCESS) {
    delete[] compressBuffer;
    return false;
  }

  // Encode all rows
  for (int y = 0; y < height; y++) {
    result = encoder.encodeLine(const_cast<uint8_t*>(bitmap + static_cast<size_t>(y) * rowBytes));
    if (result != G5_SUCCESS && result != G5_ENCODE_COMPLETE) {
      delete[] compressBuffer;
      return false;
    }
  }

  const int compressedSize = encoder.size();
  if (compressedSize <= 0 || static_cast<size_t>(compressedSize) > maxCompressedSize) {
    delete[] compressBuffer;
    return false;
  }

  // Write to file
  FsFile outFile;
  if (!SdMan.openFileForWrite("G5C", path, outFile)) {
    delete[] compressBuffer;
    return false;
  }

  // Write header
  G5ImageHeader header;
  header.magic = G5_MAGIC;
  header.width = width;
  header.height = height;
  header.compressedSize = compressedSize;

  if (outFile.write(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    outFile.close();
    SdMan.remove(path);
    delete[] compressBuffer;
    return false;
  }

  // Write compressed data
  if (outFile.write(compressBuffer, compressedSize) != static_cast<size_t>(compressedSize)) {
    outFile.close();
    SdMan.remove(path);
    delete[] compressBuffer;
    return false;
  }

  outFile.close();
  delete[] compressBuffer;
  return true;
}

bool G5ImageCache::decompressFromFile(const char* path, std::function<void(const uint8_t*, int, int)> rowCallback) {
  if (!path || !rowCallback) {
    return false;
  }

  FsFile inFile;
  if (!SdMan.openFileForRead("G5C", path, inFile)) {
    return false;
  }

  // Read header
  G5ImageHeader header;
  if (inFile.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    inFile.close();
    return false;
  }

  size_t rowBytesSize = 0;
  if (!validateHeader(header, inFile.size(), rowBytesSize) || !hasAllocationHeadroom(header.compressedSize)) {
    inFile.close();
    return false;
  }

  uint8_t* compressedData = new (std::nothrow) uint8_t[header.compressedSize];
  if (!compressedData || !hasAllocationHeadroom(rowBytesSize)) {
    delete[] compressedData;
    inFile.close();
    return false;
  }

  const int rowBytes = static_cast<int>(rowBytesSize);
  uint8_t* rowBuffer = new (std::nothrow) uint8_t[rowBytes];
  if (!rowBuffer) {
    delete[] compressedData;
    inFile.close();
    return false;
  }

  if (inFile.read(compressedData, header.compressedSize) != header.compressedSize) {
    delete[] compressedData;
    delete[] rowBuffer;
    inFile.close();
    return false;
  }
  inFile.close();

  // Decode
  G5DECODER decoder;
  int result = decoder.init(header.width, header.height, compressedData, header.compressedSize);
  if (result != G5_SUCCESS) {
    delete[] compressedData;
    delete[] rowBuffer;
    return false;
  }

  for (int y = 0; y < header.height; y++) {
    result = decoder.decodeLine(rowBuffer);
    if (result != G5_SUCCESS && result != G5_DECODE_COMPLETE) {
      delete[] compressedData;
      delete[] rowBuffer;
      return false;
    }
    rowCallback(rowBuffer, rowBytes, y);
  }

  delete[] compressedData;
  delete[] rowBuffer;
  return true;
}

bool G5ImageCache::readHeader(const char* path, G5ImageHeader& header) {
  if (!path) {
    return false;
  }

  FsFile inFile;
  if (!SdMan.openFileForRead("G5C", path, inFile)) {
    return false;
  }

  if (inFile.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    inFile.close();
    return false;
  }
  const size_t fileSize = inFile.size();
  inFile.close();

  size_t rowBytes = 0;
  return validateHeader(header, fileSize, rowBytes);
}

bool G5ImageCache::validateHeader(const G5ImageHeader& header, size_t fileSize, size_t& rowBytes) {
  if (header.magic != G5_MAGIC || header.width == 0 || header.height == 0 || header.compressedSize == 0) {
    return false;
  }

  rowBytes = (static_cast<size_t>(header.width) + 7) / 8;
  if (static_cast<size_t>(header.height) > MAX_RAW_SIZE / rowBytes || header.compressedSize > MAX_COMPRESSED_SIZE) {
    return false;
  }

  return fileSize >= sizeof(G5ImageHeader) && header.compressedSize <= fileSize - sizeof(G5ImageHeader);
}

bool G5ImageCache::hasAllocationHeadroom(size_t bytes) {
  if (bytes <= 1024) return true;
  return bytes <= heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) * 80 / 100;
}

size_t G5ImageCache::estimateMaxCompressedSize(int width, int height) {
  // Group5 can theoretically expand data in worst case (random noise)
  // Worst case: horizontal mode with long codes for every pair
  // Safe estimate: raw size + 50% overhead
  const size_t rawSize = ((static_cast<size_t>(width) + 7) / 8) * static_cast<size_t>(height);
  return rawSize + (rawSize / 2) + 1024;  // Extra margin for safety
}
