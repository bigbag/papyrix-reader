#include "PngToBmpConverter.h"

#include <Logging.h>

#define TAG "PNG"
#include <SdFat.h>
#include <esp_heap_caps.h>
#include <pngle.h>

#include <cstring>

#include "BitmapHelpers.h"

namespace {
constexpr int MAX_IMAGE_WIDTH = 2048;
constexpr int MAX_IMAGE_HEIGHT = 3072;

inline void write16(Print& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(Print& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(Print& out, const int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

void writeBmpHeader2bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 70 + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 70);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 2);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 4);
  write32(bmpOut, 4);

  uint8_t palette[16] = {
      0x00, 0x00, 0x00, 0x00,  // Black
      0x55, 0x55, 0x55, 0x00,  // Dark gray
      0xAA, 0xAA, 0xAA, 0x00,  // Light gray
      0xFF, 0xFF, 0xFF, 0x00   // White
  };
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

struct PngContext {
  FsFile* pngFile;
  Print* bmpOut;
  int srcWidth;
  int srcHeight;
  int outWidth;
  int outHeight;
  int targetMaxWidth;
  int targetMaxHeight;
  uint32_t scaleX_fp;
  uint32_t scaleY_fp;
  bool needsScaling;
  bool headerWritten;
  bool oneBit;
  bool requireDithering;
  bool initFailed;  // Set when allocation fails in pngInitCallback
  bool aborted;
  int currentSrcY;
  int currentOutY;
  uint32_t nextOutY_srcStart;
  const std::function<bool()>* shouldAbort;

  // Row buffers
  uint8_t* srcRowBuffer;  // Source row grayscale
  uint8_t* outRowBuffer;  // Output BMP row
  uint32_t* rowAccum;     // Accumulator for scaling
  uint16_t* rowCount;     // Count for scaling
  AtkinsonDitherer* ditherer;
  Atkinson1BitDitherer* oneBitDitherer;

  int bytesPerRow;
};

void writeOutputPixel(PngContext& ctx, const uint8_t gray, const int x, const int y) {
  if (ctx.oneBit) {
    const uint8_t bit =
        ctx.oneBitDitherer ? ctx.oneBitDitherer->processPixel(gray, x) : quantize1bit(adjustPixel(gray), x, y);
    ctx.outRowBuffer[x >> 3] |= static_cast<uint8_t>(bit << (7 - (x & 7)));
    return;
  }

  const uint8_t adjusted = static_cast<uint8_t>(adjustPixel(gray));
  const uint8_t level = ctx.ditherer ? ctx.ditherer->processPixel(adjusted, x) : quantize(adjusted, x, y);
  ctx.outRowBuffer[(x * 2) / 8] |= static_cast<uint8_t>(level << (6 - ((x * 2) % 8)));
}

void finishOutputRow(PngContext& ctx) {
  if (ctx.oneBitDitherer) {
    ctx.oneBitDitherer->nextRow();
  } else if (ctx.ditherer) {
    ctx.ditherer->nextRow();
  }
}

void pngDrawCallback(pngle_t* pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t rgba[4]) {
  auto* ctx = static_cast<PngContext*>(pngle_get_user_data(pngle));
  if (!ctx || ctx->initFailed || ctx->aborted || !ctx->srcRowBuffer) return;

  // Check abort at start of each row
  if (x == 0 && ctx->shouldAbort && *ctx->shouldAbort && (*ctx->shouldAbort)()) {
    ctx->aborted = true;
    return;
  }

  // Convert to grayscale using LUT
  const uint8_t gray = rgbToGray(rgba[0], rgba[1], rgba[2]);

  // Handle alpha: blend with white background
  const uint8_t alpha = rgba[3];
  const uint8_t blendedGray = (gray * alpha + 255 * (255 - alpha)) / 255;

  // Store in source row buffer
  if (x < static_cast<uint32_t>(ctx->srcWidth)) {
    ctx->srcRowBuffer[x] = blendedGray;
  }

  // Check if row complete (x is last pixel of row)
  if (x == static_cast<uint32_t>(ctx->srcWidth - 1)) {
    // Process complete row
    if (!ctx->needsScaling) {
      // Direct output
      memset(ctx->outRowBuffer, 0, ctx->bytesPerRow);
      for (int outX = 0; outX < ctx->outWidth; outX++) {
        writeOutputPixel(*ctx, ctx->srcRowBuffer[outX], outX, static_cast<int>(y));
      }
      finishOutputRow(*ctx);
      if (ctx->bmpOut->write(ctx->outRowBuffer, ctx->bytesPerRow) != static_cast<size_t>(ctx->bytesPerRow)) {
        ctx->initFailed = true;
        return;
      }
      ++ctx->currentOutY;
    } else {
      // Scaling: accumulate source pixels
      for (int outX = 0; outX < ctx->outWidth; outX++) {
        const int srcXStart = (static_cast<uint32_t>(outX) * ctx->scaleX_fp) >> 16;
        const int srcXEnd = (static_cast<uint32_t>(outX + 1) * ctx->scaleX_fp) >> 16;

        int sum = 0;
        int count = 0;
        for (int srcX = srcXStart; srcX < srcXEnd && srcX < ctx->srcWidth; srcX++) {
          sum += ctx->srcRowBuffer[srcX];
          count++;
        }
        if (count == 0 && srcXStart < ctx->srcWidth) {
          sum = ctx->srcRowBuffer[srcXStart];
          count = 1;
        }
        ctx->rowAccum[outX] += sum;
        ctx->rowCount[outX] += count;
      }

      ctx->currentSrcY++;
      const uint32_t srcY_fp = static_cast<uint32_t>(ctx->currentSrcY) << 16;

      // Output all rows whose boundaries we've crossed (handles both up- and downscaling).
      // For upscaling, one source row may produce multiple output rows.
      while (srcY_fp >= ctx->nextOutY_srcStart && ctx->currentOutY < ctx->outHeight) {
        memset(ctx->outRowBuffer, 0, ctx->bytesPerRow);
        for (int outX = 0; outX < ctx->outWidth; outX++) {
          const uint8_t gray =
              static_cast<uint8_t>((ctx->rowCount[outX] > 0) ? (ctx->rowAccum[outX] / ctx->rowCount[outX]) : 0);
          writeOutputPixel(*ctx, gray, outX, ctx->currentOutY);
        }
        finishOutputRow(*ctx);
        if (ctx->bmpOut->write(ctx->outRowBuffer, ctx->bytesPerRow) != static_cast<size_t>(ctx->bytesPerRow)) {
          ctx->initFailed = true;
          return;
        }
        ctx->currentOutY++;

        ctx->nextOutY_srcStart = static_cast<uint32_t>(ctx->currentOutY + 1) * ctx->scaleY_fp;

        // For upscaling, the next output row may pull from the same source data — keep
        // accumulators around. Only reset when advancing past this source row.
        if (srcY_fp >= ctx->nextOutY_srcStart) {
          continue;
        }
        memset(ctx->rowAccum, 0, ctx->outWidth * sizeof(uint32_t));
        memset(ctx->rowCount, 0, ctx->outWidth * sizeof(uint16_t));
      }
    }
  }
}

void pngInitCallback(pngle_t* pngle, uint32_t w, uint32_t h) {
  auto* ctx = static_cast<PngContext*>(pngle_get_user_data(pngle));
  if (!ctx) return;

  ctx->srcWidth = w;
  ctx->srcHeight = h;

  LOG_INF(TAG, "Image dimensions: %dx%d", w, h);

  if (w > MAX_IMAGE_WIDTH || h > MAX_IMAGE_HEIGHT) {
    LOG_ERR(TAG, "Image too large");
    return;
  }

  // Calculate output dimensions
  ctx->outWidth = w;
  ctx->outHeight = h;
  ctx->scaleX_fp = 65536;
  ctx->scaleY_fp = 65536;
  ctx->needsScaling = false;

  if (ctx->targetMaxWidth > 0 && ctx->targetMaxHeight > 0 &&
      (static_cast<int>(w) > ctx->targetMaxWidth || static_cast<int>(h) > ctx->targetMaxHeight)) {
    const float scaleToFitWidth = static_cast<float>(ctx->targetMaxWidth) / w;
    const float scaleToFitHeight = static_cast<float>(ctx->targetMaxHeight) / h;
    const float scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;

    ctx->outWidth = static_cast<int>(w * scale);
    ctx->outHeight = static_cast<int>(h * scale);
    if (ctx->outWidth < 1) ctx->outWidth = 1;
    if (ctx->outHeight < 1) ctx->outHeight = 1;

    ctx->scaleX_fp = (static_cast<uint32_t>(w) << 16) / ctx->outWidth;
    ctx->scaleY_fp = (static_cast<uint32_t>(h) << 16) / ctx->outHeight;
    ctx->needsScaling = true;

    LOG_INF(TAG, "Scaling %dx%d -> %dx%d", w, h, ctx->outWidth, ctx->outHeight);
  }

  // Allocate buffers
  ctx->bytesPerRow = ctx->oneBit ? (ctx->outWidth + 31) / 32 * 4 : (ctx->outWidth * 2 + 31) / 32 * 4;
  const size_t scalingBytes = ctx->needsScaling ? static_cast<size_t>(ctx->outWidth) * 6 : 0;
  const size_t ditherBytes = static_cast<size_t>(ctx->outWidth + 4) * sizeof(int16_t) * 3;
  const size_t requiredBytes =
      static_cast<size_t>(w) + static_cast<size_t>(ctx->bytesPerRow) + scalingBytes + ditherBytes;
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (requiredBytes > 1024 && requiredBytes > largest * 80 / 100) {
    LOG_ERR(TAG, "Insufficient heap for PNG conversion");
    ctx->initFailed = true;
    return;
  }

  ctx->srcRowBuffer = static_cast<uint8_t*>(malloc(w));
  ctx->outRowBuffer = static_cast<uint8_t*>(malloc(ctx->bytesPerRow));

  if (!ctx->srcRowBuffer || !ctx->outRowBuffer) {
    LOG_ERR(TAG, "Failed to allocate row buffers");
    free(ctx->srcRowBuffer);  // safe if nullptr
    free(ctx->outRowBuffer);  // safe if nullptr
    ctx->srcRowBuffer = nullptr;
    ctx->outRowBuffer = nullptr;
    ctx->initFailed = true;
    return;
  }

  if (ctx->needsScaling) {
    ctx->rowAccum = new (std::nothrow) uint32_t[ctx->outWidth]();
    ctx->rowCount = new (std::nothrow) uint16_t[ctx->outWidth]();
    if (!ctx->rowAccum || !ctx->rowCount) {
      LOG_ERR(TAG, "Failed to allocate scaling buffers");
      free(ctx->srcRowBuffer);
      free(ctx->outRowBuffer);
      delete[] ctx->rowAccum;  // safe if nullptr
      delete[] ctx->rowCount;  // safe if nullptr
      ctx->srcRowBuffer = nullptr;
      ctx->outRowBuffer = nullptr;
      ctx->rowAccum = nullptr;
      ctx->rowCount = nullptr;
      ctx->initFailed = true;
      return;
    }
    ctx->nextOutY_srcStart = ctx->scaleY_fp;
  }

  if (ctx->oneBit) {
    ctx->oneBitDitherer = new (std::nothrow) Atkinson1BitDitherer(ctx->outWidth);
    if (ctx->oneBitDitherer && !ctx->oneBitDitherer->valid()) {
      delete ctx->oneBitDitherer;
      ctx->oneBitDitherer = nullptr;
    }
    if (ctx->requireDithering && !ctx->oneBitDitherer) {
      ctx->initFailed = true;
      return;
    }
  } else {
    ctx->ditherer = new (std::nothrow) AtkinsonDitherer(ctx->outWidth);
    if (ctx->ditherer && !ctx->ditherer->valid()) {
      delete ctx->ditherer;
      ctx->ditherer = nullptr;
    }
  }
  ctx->currentSrcY = 0;
  ctx->currentOutY = 0;

  ctx->headerWritten = ctx->oneBit ? write1BitBmpHeader(*ctx->bmpOut, ctx->outWidth, ctx->outHeight)
                                   : (writeBmpHeader2bit(*ctx->bmpOut, ctx->outWidth, ctx->outHeight), true);
  if (!ctx->headerWritten) ctx->initFailed = true;
}

bool pngFileToBmpStreamInternal(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight, bool oneBit,
                                bool requireDithering, const std::function<bool()>& shouldAbort = nullptr) {
  LOG_INF(TAG, "Converting PNG to BMP (target: %dx%d)", targetMaxWidth, targetMaxHeight);

  pngle_t* pngle = pngle_new();
  if (!pngle) {
    LOG_ERR(TAG, "Failed to create pngle instance");
    return false;
  }

  PngContext ctx = {};
  ctx.pngFile = &pngFile;
  ctx.bmpOut = &bmpOut;
  ctx.targetMaxWidth = targetMaxWidth;
  ctx.targetMaxHeight = targetMaxHeight;
  ctx.headerWritten = false;
  ctx.oneBit = oneBit;
  ctx.requireDithering = requireDithering;
  ctx.aborted = false;
  ctx.shouldAbort = &shouldAbort;

  pngle_set_user_data(pngle, &ctx);
  pngle_set_init_callback(pngle, pngInitCallback);
  pngle_set_draw_callback(pngle, pngDrawCallback);

  // Read and feed PNG data
  uint8_t buffer[1024];
  int bytesRead;
  bool success = true;

  while ((bytesRead = pngFile.read(buffer, sizeof(buffer))) > 0) {
    const bool abortRequested = shouldAbort && shouldAbort();
    if (ctx.aborted || ctx.initFailed || abortRequested) {
      ctx.aborted = ctx.aborted || abortRequested;
      LOG_INF(TAG, "Abort requested during PNG conversion");
      success = false;
      break;
    }
    int fed = pngle_feed(pngle, buffer, bytesRead);
    if (fed < 0) {
      LOG_ERR(TAG, "pngle_feed error: %s", pngle_error(pngle));
      success = false;
      break;
    }
  }

  // Cleanup
  if (ctx.srcRowBuffer) free(ctx.srcRowBuffer);
  if (ctx.outRowBuffer) free(ctx.outRowBuffer);
  if (ctx.rowAccum) delete[] ctx.rowAccum;
  if (ctx.rowCount) delete[] ctx.rowCount;
  if (ctx.ditherer) delete ctx.ditherer;
  if (ctx.oneBitDitherer) delete ctx.oneBitDitherer;

  pngle_destroy(pngle);

  const bool complete = success && ctx.headerWritten && !ctx.initFailed && !ctx.aborted &&
                        ctx.currentOutY == ctx.outHeight && !(shouldAbort && shouldAbort());
  if (!complete) return false;

  LOG_INF(TAG, "Successfully converted PNG to BMP (%dx%d)", ctx.outWidth, ctx.outHeight);
  return true;
}

}  // namespace

bool PngToBmpConverter::pngFileToBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                   int targetMaxHeight, const bool oneBit, const bool requireDithering,
                                                   const std::function<bool()>& shouldAbort) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, oneBit, requireDithering,
                                    shouldAbort);
}
