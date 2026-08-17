#include "XtcCoverHelper.h"

#include <Logging.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#define TAG "XTC_COVER"

namespace xtc {
namespace {

constexpr uint32_t MAX_DIMENSION = 2000;
constexpr size_t MAX_BITMAP_SIZE = 512 * 1024;
constexpr size_t kBandsInMemory = 16;  // 8-row bands held in RAM during transpose
constexpr size_t kOneBitChunk = 1024;  // row-aligned, below parser allocation gate
constexpr size_t kPlaneChunk = 4096;   // per-plane; pairs gated by parser

bool allocationAllowed(const size_t bytes) {
  if (bytes <= 1024) return true;
  return bytes <= heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) * 80 / 100;
}

CoverResult mapParserError(const XtcError err, const std::function<bool()>& shouldAbort) {
  if (err == XtcError::OK) return CoverResult::Generated;
  if (shouldAbort && shouldAbort()) return CoverResult::TransientFailure;
  switch (err) {
    case XtcError::CORRUPTED_HEADER:
    case XtcError::INVALID_MAGIC:
    case XtcError::INVALID_VERSION:
    case XtcError::PAGE_OUT_OF_RANGE:
    case XtcError::FILE_NOT_FOUND:
      return CoverResult::InvalidFile;
    default:  // MEMORY_ERROR, READ_ERROR, CANCELLED
      return CoverResult::TransientFailure;
  }
}

void writeBmpHeader(FsFile& coverBmp, const uint16_t width, const uint16_t height) {
  // BMP header
  const uint32_t rowSize = ((width + 31) / 32) * 4;
  const uint32_t imageSize = rowSize * height;
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;

  // File header
  coverBmp.write('B');
  coverBmp.write('M');
  coverBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  // DIB header (BITMAPINFOHEADER)
  uint32_t dibHeaderSize = 40;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t widthSigned = width;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&widthSigned), 4);
  int32_t heightSigned = -static_cast<int32_t>(height);  // Negative for top-down
  coverBmp.write(reinterpret_cast<const uint8_t*>(&heightSigned), 4);
  uint16_t planes = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  // Color palette (1-bit: black and white)
  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};
  coverBmp.write(black, 4);
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};
  coverBmp.write(white, 4);
}

// 1-bit pages are row-major top-down: stream rows straight through.
CoverResult streamOneBit(XtcParser& parser, const uint16_t width, const uint16_t height, FsFile& out,
                         const std::function<bool()>& shouldAbort) {
  const size_t srcRowSize = (width + 7) / 8;
  const size_t rowsPerChunk = std::max<size_t>(1, kOneBitChunk / srcRowSize);
  const size_t chunkSize = std::min(kOneBitChunk, srcRowSize * rowsPerChunk);
  const uint32_t padBytes = static_cast<uint32_t>(((width + 31) / 32) * 4) - static_cast<uint32_t>(srcRowSize);
  const uint8_t pad[3] = {0, 0, 0};

  struct State {
    FsFile* out;
    size_t srcRowSize;
    uint32_t padBytes;
    const uint8_t* pad;
    bool ok = true;
  } state{&out, srcRowSize, padBytes, pad};

  const XtcError err = parser.loadPageStreaming(
      0,
      [&state, &shouldAbort](const uint8_t* data, const size_t size, const size_t /*offset*/) {
        if (shouldAbort && shouldAbort()) return false;
        for (size_t row = 0; row < size / state.srcRowSize; row++) {
          if (state.out->write(data + row * state.srcRowSize, state.srcRowSize) != state.srcRowSize) {
            state.ok = false;
            return false;
          }
          if (state.padBytes > 0 && state.out->write(state.pad, state.padBytes) != state.padBytes) {
            state.ok = false;
            return false;
          }
        }
        return true;
      },
      chunkSize, nullptr);

  if (err != XtcError::OK) return mapParserError(err, shouldAbort);
  return state.ok ? CoverResult::Generated : CoverResult::TransientFailure;
}

// 2-bit XTH pages are column-major right-to-left; BMP output is row-major.
// One column byte covers 8 output rows (a band). Hold kBandsInMemory bands of
// rows in RAM and make ceil(columnBytes / kBandsInMemory) sequential passes.
CoverResult streamTwoBit(XtcParser& parser, const uint16_t width, const uint16_t height, FsFile& out,
                         const std::function<bool()>& shouldAbort) {
  const size_t colBytes = xthColumnBytes(height);
  const size_t bmpRowSize = ((width + 31) / 32) * 4;
  const size_t dataRowSize = (width + 7) / 8;
  const size_t colsPerChunk = std::max<size_t>(1, kPlaneChunk / colBytes);
  const size_t chunkSize = colsPerChunk * colBytes;
  const size_t bandBufferBytes = kBandsInMemory * 8 * bmpRowSize;

  if (!allocationAllowed(bandBufferBytes)) return CoverResult::TransientFailure;
  uint8_t* const bands = static_cast<uint8_t*>(malloc(bandBufferBytes));
  if (!bands) return CoverResult::TransientFailure;

  CoverResult result = CoverResult::Generated;
  const size_t bandCount = colBytes;
  for (size_t bandBase = 0; bandBase < bandCount && result == CoverResult::Generated; bandBase += kBandsInMemory) {
    const size_t bandsActive = std::min(kBandsInMemory, bandCount - bandBase);
    memset(bands, 0, bandsActive * 8 * bmpRowSize);
    for (size_t b = 0; b < bandsActive; b++) {
      for (size_t k = 0; k < 8; k++) {
        memset(bands + (b * 8 + k) * bmpRowSize, 0xFF, dataRowSize);
      }
    }

    const XtcError err = parser.loadPagePlanePairs(
        0,
        [&](const uint8_t* plane1, const uint8_t* plane2, const size_t size, const size_t planeOffset) {
          if (shouldAbort && shouldAbort()) return false;
          const size_t firstCol = planeOffset / colBytes;
          const size_t cols = size / colBytes;
          for (size_t lc = 0; lc < cols; lc++) {
            const uint8_t* p1 = plane1 + lc * colBytes;
            const uint8_t* p2 = plane2 + lc * colBytes;
            const uint16_t x = static_cast<uint16_t>(width - 1 - (firstCol + lc));
            const size_t byte = x >> 3;
            const uint8_t mask = static_cast<uint8_t>(1u << (7 - (x & 7)));
            for (size_t b = 0; b < bandsActive; b++) {
              const uint8_t v1 = p1[bandBase + b];
              const uint8_t v2 = p2[bandBase + b];
              uint8_t* const rows = bands + b * 8 * bmpRowSize;
              for (size_t k = 0; k < 8; k++) {
                const uint8_t value = static_cast<uint8_t>((((v1 >> (7 - k)) & 1) << 1) | ((v2 >> (7 - k)) & 1));
                if (value >= 1) rows[k * bmpRowSize + byte] &= static_cast<uint8_t>(~mask);
              }
            }
          }
          return true;
        },
        chunkSize, nullptr);

    if (err != XtcError::OK) {
      result = mapParserError(err, shouldAbort);
      break;
    }

    for (size_t b = 0; b < bandsActive && result == CoverResult::Generated; b++) {
      const uint8_t* const rows = bands + b * 8 * bmpRowSize;
      for (size_t k = 0; k < 8; k++) {
        const uint32_t y = static_cast<uint32_t>((bandBase + b) * 8 + k);
        if (y >= height) break;
        if (out.write(rows + k * bmpRowSize, bmpRowSize) != bmpRowSize) {
          result = CoverResult::TransientFailure;
          break;
        }
      }
    }
  }

  free(bands);
  return result;
}

}  // namespace

void migrateStaleFailureMarkers(const std::string& cachePath, const bool purgeArtifacts) {
  const std::string migratedPath = cachePath + "/.cover.v2";
  if (SdMan.exists(migratedPath.c_str())) return;
  SdMan.remove((cachePath + "/.cover.failed").c_str());
  SdMan.remove((cachePath + "/.thumb.failed").c_str());
  SdMan.remove((cachePath + "/.cover.migrated").c_str());
  if (purgeArtifacts) {
    SdMan.remove((cachePath + "/cover.bmp").c_str());
    SdMan.remove((cachePath + "/thumb.bmp").c_str());
  }
  FsFile migrated;
  if (SdMan.openFileForWrite("XTC", migratedPath, migrated)) migrated.close();
}

CoverResult generateCoverBmpFromParser(XtcParser& parser, const std::string& coverBmpPath,
                                       const std::function<bool()>& shouldAbort) {
  if (shouldAbort && shouldAbort()) return CoverResult::TransientFailure;

  if (parser.getPageCount() == 0) {
    LOG_ERR(TAG, "No pages in XTC file");
    return CoverResult::InvalidFile;
  }

  PageInfo pageInfo;
  if (!parser.getPageInfo(0, pageInfo)) {
    LOG_ERR(TAG, "Failed to get first page info");
    return CoverResult::TransientFailure;
  }

  const uint8_t bitDepth = parser.getBitDepth();
  if (pageInfo.width == 0 || pageInfo.height == 0 || pageInfo.width > MAX_DIMENSION ||
      pageInfo.height > MAX_DIMENSION) {
    LOG_ERR(TAG, "Invalid dimensions: %ux%u", pageInfo.width, pageInfo.height);
    return CoverResult::InvalidFile;
  }

  const size_t bitmapSize =
      bitDepth == 2 ? xthBitmapSize(pageInfo.width, pageInfo.height) : xtgBitmapSize(pageInfo.width, pageInfo.height);
  if (bitmapSize > MAX_BITMAP_SIZE) {
    LOG_ERR(TAG, "Bitmap too large: %zu bytes", bitmapSize);
    return CoverResult::InvalidFile;
  }

  const std::string tempPath = coverBmpPath + ".part";
  SdMan.remove(tempPath.c_str());
  FsFile coverBmp;
  if (!SdMan.openFileForWrite("XTC", tempPath, coverBmp)) {
    LOG_ERR(TAG, "Failed to create temporary cover BMP file");
    return CoverResult::TransientFailure;
  }

  writeBmpHeader(coverBmp, pageInfo.width, pageInfo.height);

  CoverResult result = bitDepth == 2 ? streamTwoBit(parser, pageInfo.width, pageInfo.height, coverBmp, shouldAbort)
                                     : streamOneBit(parser, pageInfo.width, pageInfo.height, coverBmp, shouldAbort);
  coverBmp.close();

  if (shouldAbort && shouldAbort()) result = CoverResult::TransientFailure;
  if (result != CoverResult::Generated || !SdMan.commitFile(tempPath.c_str(), coverBmpPath.c_str())) {
    SdMan.remove(tempPath.c_str());
    return result == CoverResult::Generated ? CoverResult::TransientFailure : result;
  }

  LOG_INF(TAG, "Generated cover BMP: %s", coverBmpPath.c_str());
  return CoverResult::Generated;
}

}  // namespace xtc
