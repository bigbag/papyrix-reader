#include "XtcPageRenderer.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Xtc/XtcParser.h>
#include <esp_task_wdt.h>

#include <cstdlib>
#include <cstring>

#define TAG "XTC_RENDER"

namespace papyrix {

constexpr uint16_t MAX_PAGE_WIDTH = 2048;
constexpr uint16_t MAX_PAGE_HEIGHT = 2048;

XtcPageRenderer::XtcPageRenderer(GfxRenderer& renderer) : renderer_(renderer) {}

XtcPageRenderer::RenderResult XtcPageRenderer::render(xtc::XtcParser& parser, uint32_t pageNum,
                                                      std::function<void()> refreshCallback) {
  // Bounds check
  if (pageNum >= parser.getPageCount()) {
    return RenderResult::EndOfBook;
  }

  const uint16_t pageWidth = parser.getWidth();
  const uint16_t pageHeight = parser.getHeight();
  const uint8_t bitDepth = parser.getBitDepth();

  if (pageWidth == 0 || pageHeight == 0 || pageWidth > MAX_PAGE_WIDTH || pageHeight > MAX_PAGE_HEIGHT) {
    LOG_ERR(TAG, "Invalid page dimensions");
    return RenderResult::InvalidDimensions;
  }

  // Calculate buffer size based on bit depth
  const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
  size_t bufferSize;
  uint8_t* plane1Buffer = nullptr;
  uint8_t* plane2Buffer = nullptr;

  if (bitDepth == 2) {
    // Split allocation: allocate two separate buffers to handle heap fragmentation
    // Two 48KB blocks are easier to find than one 96KB contiguous block
    plane1Buffer = static_cast<uint8_t*>(malloc(planeSize));
    if (!plane1Buffer) {
      LOG_ERR(TAG, "Failed to allocate plane1 buffer (%zu bytes, free heap: %lu)", planeSize,
              static_cast<unsigned long>(ESP.getFreeHeap()));
      return RenderResult::AllocationFailed;
    }

    plane2Buffer = static_cast<uint8_t*>(malloc(planeSize));
    if (!plane2Buffer) {
      LOG_ERR(TAG, "Failed to allocate plane2 buffer (%zu bytes, free heap: %lu)", planeSize,
              static_cast<unsigned long>(ESP.getFreeHeap()));
      free(plane1Buffer);
      return RenderResult::AllocationFailed;
    }

    bufferSize = planeSize * 2;
  } else {
    bufferSize = static_cast<size_t>((pageWidth + 7) / 8) * pageHeight;
    plane1Buffer = static_cast<uint8_t*>(malloc(bufferSize));
    if (!plane1Buffer) {
      LOG_ERR(TAG, "Failed to allocate buffer (%zu bytes, free heap: %lu)", bufferSize,
              static_cast<unsigned long>(ESP.getFreeHeap()));
      return RenderResult::AllocationFailed;
    }
  }

  // Load page data
  size_t bytesRead = 0;
  if (bitDepth == 2) {
    // Use streaming to load into separate buffers
    xtc::XtcError err = parser.loadPageStreaming(
        pageNum,
        [&](const uint8_t* data, size_t size, size_t offset) {
          // Direct data to the appropriate buffer based on offset
          size_t remaining = size;
          size_t srcOffset = 0;

          while (remaining > 0) {
            size_t currentOffset = offset + srcOffset;
            if (currentOffset < planeSize) {
              // Writing to plane1
              size_t toWrite = std::min(remaining, planeSize - currentOffset);
              memcpy(plane1Buffer + currentOffset, data + srcOffset, toWrite);
              srcOffset += toWrite;
              remaining -= toWrite;
            } else {
              // Writing to plane2
              size_t plane2Offset = currentOffset - planeSize;
              size_t toWrite = std::min(remaining, planeSize - plane2Offset);
              memcpy(plane2Buffer + plane2Offset, data + srcOffset, toWrite);
              srcOffset += toWrite;
              remaining -= toWrite;
            }
          }
          bytesRead += size;
        },
        4096);

    if (err != xtc::XtcError::OK) {
      LOG_ERR(TAG, "Failed to load page %u (streaming error)", pageNum);
      free(plane1Buffer);
      free(plane2Buffer);
      return RenderResult::PageLoadFailed;
    }
  } else {
    bytesRead = parser.loadPage(pageNum, plane1Buffer, bufferSize);
    if (bytesRead == 0) {
      LOG_ERR(TAG, "Failed to load page %u", pageNum);
      free(plane1Buffer);
      return RenderResult::PageLoadFailed;
    }
  }

  // Clear screen and render
  renderer_.clearScreen();

  if (bitDepth == 2) {
    render2BitGrayscale(plane1Buffer, plane2Buffer, pageWidth, pageHeight);
    refreshCallback();

    // Grayscale rendering requires additional passes
    const uint8_t* plane1 = plane1Buffer;
    const uint8_t* plane2 = plane2Buffer;
    const size_t colBytes = (pageHeight + 7) / 8;

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    // Pass 2: LSB buffer - mark DARK gray only (value 1)
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

    // Display grayscale overlay
    renderer_.displayGrayBuffer();

    // Pass 4: Re-render BW to framebuffer (restore for next frame)
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
    refreshCallback();
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

void XtcPageRenderer::render2BitGrayscale(const uint8_t* plane1, const uint8_t* plane2, uint16_t width,
                                          uint16_t height) {
  // XTCH 2-bit mode: Two bit planes, column-major order
  // - Columns scanned right to left (x = width-1 down to 0)
  // - 8 vertical pixels per byte (MSB = topmost pixel in group)
  // - First plane: Bit1, Second plane: Bit2
  // - Pixel value = (bit1 << 1) | bit2
  // - Grayscale: 0=White, 1=Dark Grey, 2=Light Grey, 3=Black

  const size_t colBytes = (height + 7) / 8;

  // Pass 1: BW buffer - draw all non-white pixels as black
  for (uint16_t y = 0; y < height; y++) {
    for (uint16_t x = 0; x < width; x++) {
      const size_t colIndex = width - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      const uint8_t pixelValue = (bit1 << 1) | bit2;

      if (pixelValue >= 1) {
        renderer_.drawPixel(x, y, true);
      }
    }
  }
}

}  // namespace papyrix
