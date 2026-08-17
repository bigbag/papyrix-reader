/**
 * XtcTypes.h
 *
 * XTC file format type definitions
 * XTC ebook support for CrossPoint Reader
 *
 * XTC is the native binary ebook format for XTeink X4 e-reader.
 * It stores pre-rendered bitmap images per page.
 *
 * Format based on EPUB2XTC converter by Rafal-P-Mazur
 */

#pragma once

#include <FsHelpers.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace xtc {

// XTC file magic numbers (little-endian)
// "XTC\0" = 0x58, 0x54, 0x43, 0x00
constexpr uint32_t XTC_MAGIC = 0x00435458;  // "XTC\0" in little-endian (1-bit fast mode)
// "XTCH" = 0x58, 0x54, 0x43, 0x48
constexpr uint32_t XTCH_MAGIC = 0x48435458;  // "XTCH" in little-endian (2-bit high quality mode)
// "XTG\0" = 0x58, 0x54, 0x47, 0x00
constexpr uint32_t XTG_MAGIC = 0x00475458;  // "XTG\0" for 1-bit page data
// "XTH\0" = 0x58, 0x54, 0x48, 0x00
constexpr uint32_t XTH_MAGIC = 0x00485458;  // "XTH\0" for 2-bit page data

// XTeink X4 display resolution
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 800;

// Safety limits for untrusted page metadata
constexpr uint16_t MAX_XTC_PAGE_COUNT = 10000;
constexpr uint16_t MAX_XTC_DIMENSION = 2048;
constexpr size_t XTC_MAX_BITMAP_SIZE = 1024 * 1024;

// Metadata block layout relative to XtcHeader::metadataOffset.
constexpr uint64_t METADATA_TITLE_OFFSET = 0;
constexpr size_t METADATA_TITLE_SIZE = 128;
constexpr uint64_t METADATA_AUTHOR_OFFSET = 128;
constexpr size_t METADATA_AUTHOR_SIZE = 64;
constexpr uint64_t METADATA_CHAPTER_COUNT_OFFSET = 196;

constexpr size_t xthColumnBytes(uint16_t height) { return (static_cast<size_t>(height) + 7) / 8; }
constexpr size_t xthPlaneSize(uint16_t width, uint16_t height) {
  return static_cast<size_t>(width) * xthColumnBytes(height);
}
constexpr size_t xthBitmapSize(uint16_t width, uint16_t height) { return xthPlaneSize(width, height) * 2; }
constexpr uint8_t xthBaseByte(const uint8_t plane1, const uint8_t plane2) {
  return static_cast<uint8_t>(~(plane1 | plane2));
}
constexpr uint8_t xthLsbByte(const uint8_t plane1, const uint8_t plane2) {
  return static_cast<uint8_t>(~plane1 & plane2);
}
constexpr uint8_t xthMsbByte(const uint8_t plane1, const uint8_t plane2) { return plane1 ^ plane2; }
constexpr size_t xtgBitmapSize(uint16_t width, uint16_t height) {
  return ((static_cast<size_t>(width) + 7) / 8) * height;
}

inline uint8_t xthPixelValue(const uint8_t* plane1, const uint8_t* plane2, uint16_t width, uint16_t height, uint16_t x,
                             uint16_t y) {
  const size_t byteOffset = static_cast<size_t>(width - 1 - x) * xthColumnBytes(height) + y / 8;
  const uint8_t shift = static_cast<uint8_t>(7 - y % 8);
  return static_cast<uint8_t>((((plane1[byteOffset] >> shift) & 1) << 1) | ((plane2[byteOffset] >> shift) & 1));
}

// XTC file header (56 bytes)
#pragma pack(push, 1)
struct XtcHeader {
  uint32_t magic;            // 0x00: Magic number "XTC\0" (0x00435458)
  uint8_t versionMajor;      // 0x04: Format version major (typically 1) (together with minor = 1.0)
  uint8_t versionMinor;      // 0x05: Format version minor (typically 0)
  uint16_t pageCount;        // 0x06: Total page count
  uint8_t readDirection;     // 0x08: Reading direction
  uint8_t hasMetadata;       // 0x09: Metadata presence flag
  uint8_t hasThumbnails;     // 0x0A: Thumbnail presence flag
  uint8_t hasChapters;       // 0x0B: Chapter presence flag
  uint32_t currentPage;      // 0x0C: Current page (1-based)
  uint64_t metadataOffset;   // 0x10: Metadata offset
  uint64_t pageTableOffset;  // 0x18: Page table offset
  uint64_t dataOffset;       // 0x20: First page data offset
  uint64_t thumbOffset;      // 0x28: Thumbnail offset
  uint64_t chapterOffset;    // 0x30: Chapter data offset
};
#pragma pack(pop)

static_assert(sizeof(XtcHeader) == 56);
static_assert(offsetof(XtcHeader, hasChapters) == 0x0B);
static_assert(offsetof(XtcHeader, pageTableOffset) == 0x18);
static_assert(offsetof(XtcHeader, chapterOffset) == 0x30);

// Page table entry (16 bytes per page)
#pragma pack(push, 1)
struct PageTableEntry {
  uint64_t dataOffset;  // 0x00: Absolute offset to page data
  uint32_t dataSize;    // 0x08: Page data size in bytes
  uint16_t width;       // 0x0C: Page width (480)
  uint16_t height;      // 0x0E: Page height (800)
};
#pragma pack(pop)

// XTG/XTH page data header (22 bytes)
// Used for both 1-bit (XTG) and 2-bit (XTH) formats
#pragma pack(push, 1)
struct XtgPageHeader {
  uint32_t magic;       // 0x00: File identifier (XTG: 0x00475458, XTH: 0x00485458)
  uint16_t width;       // 0x04: Image width (pixels)
  uint16_t height;      // 0x06: Image height (pixels)
  uint8_t colorMode;    // 0x08: Color mode (0=monochrome)
  uint8_t compression;  // 0x09: Compression (0=uncompressed)
  uint32_t dataSize;    // 0x0A: Image data size (bytes)
  uint64_t md5;         // 0x0E: MD5 checksum (first 8 bytes, optional)
  // Followed by bitmap data at offset 0x16 (22)
  //
  // XTG (1-bit): Row-major, 8 pixels/byte, MSB first
  //   dataSize = ((width + 7) / 8) * height
  //
  // XTH (2-bit): Two bit planes, column-major (right-to-left), 8 vertical pixels/byte
  //   Each column is padded to a whole byte.
  //   dataSize = width * ((height + 7) / 8) * 2
  //   First plane: Bit1 for all pixels
  //   Second plane: Bit2 for all pixels
  //   pixelValue = (bit1 << 1) | bit2
};
#pragma pack(pop)

// Page information (internal use, optimized for memory)
struct PageInfo {
  uint64_t offset;   // File offset to page data
  uint32_t size;     // Data size (bytes)
  uint16_t width;    // Page width
  uint16_t height;   // Page height
  uint8_t bitDepth;  // 1 = XTG (1-bit), 2 = XTH (2-bit grayscale)
  uint8_t padding;   // Alignment padding
};

struct ChapterInfo {
  std::string name;
  uint16_t startPage;
  uint16_t endPage;
};

// Error codes
enum class XtcError {
  OK = 0,
  FILE_NOT_FOUND,
  INVALID_MAGIC,
  INVALID_VERSION,
  CORRUPTED_HEADER,
  PAGE_OUT_OF_RANGE,
  READ_ERROR,
  WRITE_ERROR,
  MEMORY_ERROR,
  DECOMPRESSION_ERROR,
  CANCELLED,
};

// Convert error code to string
inline const char* errorToString(XtcError err) {
  switch (err) {
    case XtcError::OK:
      return "OK";
    case XtcError::FILE_NOT_FOUND:
      return "File not found";
    case XtcError::INVALID_MAGIC:
      return "Invalid magic number";
    case XtcError::INVALID_VERSION:
      return "Unsupported version";
    case XtcError::CORRUPTED_HEADER:
      return "Corrupted header";
    case XtcError::PAGE_OUT_OF_RANGE:
      return "Page out of range";
    case XtcError::READ_ERROR:
      return "Read error";
    case XtcError::WRITE_ERROR:
      return "Write error";
    case XtcError::MEMORY_ERROR:
      return "Memory allocation error";
    case XtcError::DECOMPRESSION_ERROR:
      return "Decompression error";
    case XtcError::CANCELLED:
      return "Cancelled";
    default:
      return "Unknown error";
  }
}

/**
 * Check if filename has XTC/XTCH extension
 */
inline bool isXtcExtension(const char* filename) { return FsHelpers::isXtcFile(filename); }

}  // namespace xtc
