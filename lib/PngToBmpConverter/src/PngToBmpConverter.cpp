#include "PngToBmpConverter.h"

#include <Logging.h>

#define TAG "PNG"
#include <SdFat.h>
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
  bool quickMode;   // Fast preview: simple threshold instead of dithering
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

  int bytesPerRow;
};

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
        const uint8_t gray = adjustPixel(ctx->srcRowBuffer[outX]);
        uint8_t twoBit;
        if (ctx->quickMode) {
          // Simple threshold quantization (faster)
          twoBit = quantizeSimple(gray);
        } else {
          twoBit = ctx->ditherer ? ctx->ditherer->processPixel(gray, outX) : quantize(gray, outX, y);
        }
        const int byteIndex = (outX * 2) / 8;
        const int bitOffset = 6 - ((outX * 2) % 8);
        ctx->outRowBuffer[byteIndex] |= (twoBit << bitOffset);
      }
      if (ctx->ditherer && !ctx->quickMode) ctx->ditherer->nextRow();
      ctx->bmpOut->write(ctx->outRowBuffer, ctx->bytesPerRow);
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

      if (srcY_fp >= ctx->nextOutY_srcStart && ctx->currentOutY < ctx->outHeight) {
        memset(ctx->outRowBuffer, 0, ctx->bytesPerRow);
        for (int outX = 0; outX < ctx->outWidth; outX++) {
          const uint8_t gray = adjustPixel((ctx->rowCount[outX] > 0) ? (ctx->rowAccum[outX] / ctx->rowCount[outX]) : 0);
          uint8_t twoBit;
          if (ctx->quickMode) {
            // Simple threshold quantization (faster)
            twoBit = quantizeSimple(gray);
          } else {
            twoBit = ctx->ditherer ? ctx->ditherer->processPixel(gray, outX) : quantize(gray, outX, ctx->currentOutY);
          }
          const int byteIndex = (outX * 2) / 8;
          const int bitOffset = 6 - ((outX * 2) % 8);
          ctx->outRowBuffer[byteIndex] |= (twoBit << bitOffset);
        }
        if (ctx->ditherer && !ctx->quickMode) ctx->ditherer->nextRow();
        ctx->bmpOut->write(ctx->outRowBuffer, ctx->bytesPerRow);
        ctx->currentOutY++;

        memset(ctx->rowAccum, 0, ctx->outWidth * sizeof(uint32_t));
        memset(ctx->rowCount, 0, ctx->outWidth * sizeof(uint16_t));
        ctx->nextOutY_srcStart = static_cast<uint32_t>(ctx->currentOutY + 1) * ctx->scaleY_fp;
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
  ctx->srcRowBuffer = static_cast<uint8_t*>(malloc(w));
  ctx->bytesPerRow = (ctx->outWidth * 2 + 31) / 32 * 4;
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

  // Skip ditherer allocation in quickMode for faster preview
  if (!ctx->quickMode) {
    ctx->ditherer = new (std::nothrow) AtkinsonDitherer(ctx->outWidth);
    if (!ctx->ditherer) {
      LOG_ERR(TAG, "Failed to allocate ditherer");
      free(ctx->srcRowBuffer);
      free(ctx->outRowBuffer);
      delete[] ctx->rowAccum;
      delete[] ctx->rowCount;
      ctx->srcRowBuffer = nullptr;
      ctx->outRowBuffer = nullptr;
      ctx->rowAccum = nullptr;
      ctx->rowCount = nullptr;
      ctx->initFailed = true;
      return;
    }
  }
  ctx->currentSrcY = 0;
  ctx->currentOutY = 0;

  // Write BMP header
  writeBmpHeader2bit(*ctx->bmpOut, ctx->outWidth, ctx->outHeight);
  ctx->headerWritten = true;
}

bool pngFileToBmpStreamInternal(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight, bool quickMode,
                                const std::function<bool()>& shouldAbort = nullptr) {
  LOG_INF(TAG, "Converting PNG to BMP (target: %dx%d)%s", targetMaxWidth, targetMaxHeight, quickMode ? " [QUICK]" : "");

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
  ctx.quickMode = quickMode;
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
    if (ctx.aborted) {
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

  pngle_destroy(pngle);

  if (success && ctx.headerWritten) {
    LOG_INF(TAG, "Successfully converted PNG to BMP (%dx%d)", ctx.outWidth, ctx.outHeight);
    return true;
  }

  return false;
}

}  // namespace

bool PngToBmpConverter::pngFileToBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                   int targetMaxHeight, const std::function<bool()>& shouldAbort) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, false, shouldAbort);
}

bool PngToBmpConverter::pngFileToBmpStreamQuick(FsFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                int targetMaxHeight) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, true);
}
