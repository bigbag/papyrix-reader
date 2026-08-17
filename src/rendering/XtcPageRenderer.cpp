#include "XtcPageRenderer.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Xtc/XtcParser.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include <cstdlib>
#include <cstring>

#define TAG "XTC_RENDER"

namespace papyrix {

XtcPageRenderer::XtcPageRenderer(GfxRenderer& renderer) : renderer_(renderer) {}

XtcPageRenderer::RenderResult XtcPageRenderer::render(xtc::XtcParser& parser, uint32_t pageNum,
                                                      const RefreshCallback& refreshCallback) {
  // Bounds check
  if (pageNum >= parser.getPageCount()) {
    return RenderResult::EndOfBook;
  }

  xtc::PageInfo pageInfo;
  if (!parser.getPageInfo(pageNum, pageInfo)) {
    return RenderResult::PageLoadFailed;
  }

  const uint16_t pageWidth = pageInfo.width;
  const uint16_t pageHeight = pageInfo.height;
  const uint8_t bitDepth = pageInfo.bitDepth;

  if (pageWidth == 0 || pageHeight == 0 || pageWidth > xtc::MAX_XTC_DIMENSION || pageHeight > xtc::MAX_XTC_DIMENSION) {
    LOG_ERR(TAG, "Invalid page dimensions");
    return RenderResult::InvalidDimensions;
  }

  const size_t planeSize = bitDepth == 2 ? xtc::xthPlaneSize(pageWidth, pageHeight) : 0;
  size_t bufferSize;
  uint8_t* plane1Buffer = nullptr;
  uint8_t* plane2Buffer = nullptr;

  auto allocateBuffer = [](size_t needed) -> uint8_t* {
    if (needed > 1024 && needed > heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) * 80 / 100) {
      return nullptr;
    }
    return static_cast<uint8_t*>(malloc(needed));
  };

  if (bitDepth == 2) {
    // Split allocation: allocate two separate buffers to handle heap fragmentation
    // Two 48KB blocks are easier to find than one 96KB contiguous block
    plane1Buffer = allocateBuffer(planeSize);
    if (!plane1Buffer) {
      LOG_ERR(TAG, "Failed to allocate plane1 buffer (%zu bytes, free heap: %lu)", planeSize,
              static_cast<unsigned long>(ESP.getFreeHeap()));
      return RenderResult::AllocationFailed;
    }

    plane2Buffer = allocateBuffer(planeSize);
    if (!plane2Buffer) {
      LOG_ERR(TAG, "Failed to allocate plane2 buffer (%zu bytes, free heap: %lu)", planeSize,
              static_cast<unsigned long>(ESP.getFreeHeap()));
      free(plane1Buffer);
      return RenderResult::AllocationFailed;
    }

    bufferSize = planeSize * 2;
  } else {
    bufferSize = xtc::xtgBitmapSize(pageWidth, pageHeight);
    plane1Buffer = allocateBuffer(bufferSize);
    if (!plane1Buffer) {
      LOG_ERR(TAG, "Failed to allocate buffer (%zu bytes, free heap: %lu)", bufferSize,
              static_cast<unsigned long>(ESP.getFreeHeap()));
      return RenderResult::AllocationFailed;
    }
  }

  // Load page data
  size_t bytesRead = 0;
  if (bitDepth == 2) {
    bool streamOverflow = false;
    // Use streaming to load into separate buffers
    xtc::XtcError err = parser.loadPageStreaming(
        pageNum,
        [&](const uint8_t* data, size_t size, size_t offset) {
          if (offset > bufferSize || size > bufferSize - offset) {
            streamOverflow = true;
            return false;
          }

          size_t plane1Bytes = 0;
          if (offset < planeSize) {
            plane1Bytes = std::min(size, planeSize - offset);
            memcpy(plane1Buffer + offset, data, plane1Bytes);
          }

          const size_t plane2Bytes = size - plane1Bytes;
          if (plane2Bytes > 0) {
            const size_t plane2Offset = offset + plane1Bytes - planeSize;
            memcpy(plane2Buffer + plane2Offset, data + plane1Bytes, plane2Bytes);
          }
          bytesRead += size;
          return true;
        },
        4096);

    if (err != xtc::XtcError::OK || streamOverflow || bytesRead != bufferSize) {
      LOG_ERR(TAG, "Failed to load page %u (streaming error)", pageNum);
      free(plane1Buffer);
      free(plane2Buffer);
      return RenderResult::PageLoadFailed;
    }
  } else {
    bytesRead = parser.loadPage(pageNum, plane1Buffer, bufferSize);
    if (bytesRead != bufferSize) {
      LOG_ERR(TAG, "Failed to load page %u", pageNum);
      free(plane1Buffer);
      return RenderResult::PageLoadFailed;
    }
  }

  // Clear screen and render
  renderer_.clearScreen();

  if (bitDepth == 2) {
    // Grayscale rendering requires additional passes
    const uint8_t* plane1 = plane1Buffer;
    const uint8_t* plane2 = plane2Buffer;

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      return xtc::xthPixelValue(plane1, plane2, pageWidth, pageHeight, x, y);
    };

    // Pass 1: Black-and-white rendering
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer_.drawPixel(x, y, true);
        }
      }
      if (y % 100 == 0) esp_task_wdt_reset();
    }
    refreshCallback(RefreshRequest::GrayscaleBase);

    // Pass 2: LSB buffer - mark DARK gray (value 1)
    renderer_.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) == 1) {
          renderer_.drawPixel(x, y, false);
        }
      }
      if (y % 100 == 0) esp_task_wdt_reset();
    }
    renderer_.copyGrayscaleLsbBuffers();

    // Pass 3: MSB buffer - mark LIGHT AND DARK gray (value 1 or 2)
    renderer_.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        const uint8_t pv = getPixelValue(x, y);
        if (pv == 1 || pv == 2) {
          renderer_.drawPixel(x, y, false);
        }
      }
      if (y % 100 == 0) esp_task_wdt_reset();
    }
    renderer_.copyGrayscaleMsbBuffers();

    renderer_.displayGrayBuffer();

    // Restore framebuffer and sync hardware RAMs so the next page turn
    // does a clean differential refresh instead of relying on grayscaleRevert()
    // (which leaves stale MSB data in RED RAM in single-buffer mode).
    renderer_.clearScreen();
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer_.drawPixel(x, y, true);
        }
      }
      if (y % 100 == 0) esp_task_wdt_reset();
    }
    renderer_.cleanupGrayscaleWithFrameBuffer();
    LOG_DBG(TAG, "Rendered page %u/%u (2-bit grayscale)", pageNum + 1, parser.getPageCount());
    free(plane2Buffer);
  } else {
    render1Bit(plane1Buffer, pageWidth, pageHeight);
    refreshCallback(RefreshRequest::Cadenced);
    LOG_DBG(TAG, "Rendered page %u/%u (%u-bit)", pageNum + 1, parser.getPageCount(), bitDepth);
  }

  free(plane1Buffer);
  return RenderResult::Success;
}

void XtcPageRenderer::render1Bit(const uint8_t* buffer, uint16_t width, uint16_t height) {
  const size_t srcRowBytes = (width + 7) / 8;

  for (uint16_t srcY = 0; srcY < height; srcY++) {
    const size_t srcRowStart = srcY * srcRowBytes;

    for (size_t byteIdx = 0; byteIdx < srcRowBytes; byteIdx++) {
      const uint8_t byte = buffer[srcRowStart + byteIdx];

      // Fast path: all white (0xFF) - skip entirely
      if (byte == 0xFF) continue;

      const uint16_t baseX = byteIdx * 8;

      // Fast path: all black (0x00) - draw all 8 pixels
      if (byte == 0x00) {
        for (int bit = 0; bit < 8 && baseX + bit < width; bit++) {
          renderer_.drawPixel(baseX + bit, srcY, true);
        }
        continue;
      }

      // Mixed byte - process individual bits (MSB first, bit 7 = leftmost)
      for (int bit = 7; bit >= 0; bit--) {
        const uint16_t x = baseX + (7 - bit);
        if (x >= width) break;
        if (!((byte >> bit) & 1)) {  // XTC: 0 = black, 1 = white
          renderer_.drawPixel(x, srcY, true);
        }
      }
    }
  }
}

}  // namespace papyrix
