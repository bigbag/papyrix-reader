#include "XtcPageRenderer.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Xtc/XtcParser.h>
#include <esp_task_wdt.h>

#define TAG "XTC_RENDER"

namespace papyrix {
namespace {

XtcPageRenderer::RenderResult mapStreamError(const xtc::XtcError error) {
  return error == xtc::XtcError::MEMORY_ERROR ? XtcPageRenderer::RenderResult::AllocationFailed
                                              : XtcPageRenderer::RenderResult::PageLoadFailed;
}

}  // namespace

XtcPageRenderer::XtcPageRenderer(GfxRenderer& renderer) : renderer_(renderer) {}

XtcPageRenderer::RenderResult XtcPageRenderer::render(xtc::XtcParser& parser, const uint32_t pageNum,
                                                      const RefreshCallback& refreshCallback) {
  if (pageNum >= parser.getPageCount()) return RenderResult::EndOfBook;

  xtc::PageInfo pageInfo;
  if (!parser.getPageInfo(pageNum, pageInfo)) return RenderResult::PageLoadFailed;

  const uint16_t width = pageInfo.width;
  const uint16_t height = pageInfo.height;
  if (width == 0 || height == 0 || width > xtc::MAX_XTC_DIMENSION || height > xtc::MAX_XTC_DIMENSION) {
    LOG_ERR(TAG, "Invalid page dimensions");
    return RenderResult::InvalidDimensions;
  }

  if (pageInfo.bitDepth == 1) return render1Bit(parser, pageNum, width, height, refreshCallback);
  if (pageInfo.bitDepth == 2) return render2Bit(parser, pageNum, width, height, refreshCallback);
  return RenderResult::InvalidDimensions;
}

XtcPageRenderer::RenderResult XtcPageRenderer::render1Bit(xtc::XtcParser& parser, const uint32_t pageNum,
                                                          const uint16_t width, const uint16_t height,
                                                          const RefreshCallback& refreshCallback) {
  const size_t rowBytes = (static_cast<size_t>(width) + 7) / 8;
  const size_t bitmapSize = rowBytes * height;
  bool streamOverflow = false;

  renderer_.clearScreen();
  const xtc::XtcError error = parser.loadPageStreaming(
      pageNum,
      [&](const uint8_t* data, const size_t size, const size_t offset) {
        if (offset > bitmapSize || size > bitmapSize - offset) {
          streamOverflow = true;
          return false;
        }

        for (size_t i = 0; i < size; ++i) {
          const size_t byteOffset = offset + i;
          const uint16_t y = static_cast<uint16_t>(byteOffset / rowBytes);
          const uint16_t baseX = static_cast<uint16_t>((byteOffset % rowBytes) * 8);
          if (y >= height || y >= renderer_.getScreenHeight()) continue;

          const uint8_t byte = data[i];
          if (byte == 0xFF) continue;
          for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint16_t x = baseX + bit;
            if (x >= width || x >= renderer_.getScreenWidth()) break;
            if ((byte & static_cast<uint8_t>(0x80U >> bit)) == 0) renderer_.drawPixel(x, y, true);
          }
        }
        esp_task_wdt_reset();
        return true;
      },
      4096);

  if (error != xtc::XtcError::OK || streamOverflow) {
    renderer_.clearScreen();
    LOG_ERR(TAG, "Failed to stream page %u", pageNum);
    return error == xtc::XtcError::OK ? RenderResult::PageLoadFailed : mapStreamError(error);
  }

  refreshCallback(RefreshRequest::Cadenced);
  LOG_DBG(TAG, "Rendered page %u/%u (1-bit)", pageNum + 1, parser.getPageCount());
  return RenderResult::Success;
}

XtcPageRenderer::RenderResult XtcPageRenderer::render2Bit(xtc::XtcParser& parser, const uint32_t pageNum,
                                                          const uint16_t width, const uint16_t height,
                                                          const RefreshCallback& refreshCallback) {
  RenderResult result = compose2BitPass(parser, pageNum, width, height, GrayscalePass::Base);
  if (result != RenderResult::Success) {
    renderer_.clearScreen();
    return result;
  }
  refreshCallback(RefreshRequest::GrayscaleBase);

  result = compose2BitPass(parser, pageNum, width, height, GrayscalePass::Lsb);
  if (result != RenderResult::Success) {
    recoverGrayscaleFailure();
    return result;
  }
  renderer_.copyGrayscaleLsbBuffers();

  result = compose2BitPass(parser, pageNum, width, height, GrayscalePass::Msb);
  if (result != RenderResult::Success) {
    recoverGrayscaleFailure();
    return result;
  }
  renderer_.copyGrayscaleMsbBuffers();
  renderer_.displayGrayBuffer();

  result = compose2BitPass(parser, pageNum, width, height, GrayscalePass::Base);
  if (result != RenderResult::Success) {
    recoverGrayscaleFailure();
    return result;
  }
  renderer_.cleanupGrayscaleWithFrameBuffer();

  LOG_DBG(TAG, "Rendered page %u/%u (2-bit grayscale)", pageNum + 1, parser.getPageCount());
  return RenderResult::Success;
}

XtcPageRenderer::RenderResult XtcPageRenderer::compose2BitPass(xtc::XtcParser& parser, const uint32_t pageNum,
                                                               const uint16_t width, const uint16_t height,
                                                               const GrayscalePass pass) {
  const size_t columnBytes = xtc::xthColumnBytes(height);
  const size_t planeSize = xtc::xthPlaneSize(width, height);
  const bool nativeLayout = usesNativeXthLayout(width, height, planeSize);
  bool streamOverflow = false;

  renderer_.clearScreen(pass == GrayscalePass::Base ? 0xFF : 0x00);
  uint8_t* const frameBuffer = renderer_.getFrameBuffer();
  const xtc::XtcError error = parser.loadPagePlanePairs(
      pageNum,
      [&](const uint8_t* plane1, const uint8_t* plane2, const size_t size, const size_t offset) {
        if (offset > planeSize || size > planeSize - offset) {
          streamOverflow = true;
          return false;
        }

        if (nativeLayout) {
          for (size_t i = 0; i < size; ++i) {
            switch (pass) {
              case GrayscalePass::Base:
                frameBuffer[offset + i] = xtc::xthBaseByte(plane1[i], plane2[i]);
                break;
              case GrayscalePass::Lsb:
                frameBuffer[offset + i] = xtc::xthLsbByte(plane1[i], plane2[i]);
                break;
              case GrayscalePass::Msb:
                frameBuffer[offset + i] = xtc::xthMsbByte(plane1[i], plane2[i]);
                break;
            }
          }
        } else {
          for (size_t i = 0; i < size; ++i) {
            const size_t byteOffset = offset + i;
            const size_t sourceColumn = byteOffset / columnBytes;
            if (sourceColumn >= width) {
              streamOverflow = true;
              return false;
            }
            const uint16_t x = static_cast<uint16_t>(width - 1 - sourceColumn);
            const size_t byteY = byteOffset % columnBytes;
            if (x >= renderer_.getScreenWidth()) continue;

            for (uint8_t bit = 0; bit < 8; ++bit) {
              const uint16_t y = static_cast<uint16_t>(byteY * 8 + bit);
              if (y >= height) break;
              if (y >= renderer_.getScreenHeight()) continue;

              const uint8_t mask = static_cast<uint8_t>(0x80U >> bit);
              const uint8_t value = static_cast<uint8_t>(((plane1[i] & mask) ? 2 : 0) | ((plane2[i] & mask) ? 1 : 0));
              if (pass == GrayscalePass::Base && value >= 1) {
                renderer_.drawPixel(x, y, true);
              } else if (pass == GrayscalePass::Lsb && value == 1) {
                renderer_.drawPixel(x, y, false);
              } else if (pass == GrayscalePass::Msb && (value == 1 || value == 2)) {
                renderer_.drawPixel(x, y, false);
              }
            }
          }
        }

        esp_task_wdt_reset();
        return true;
      },
      4096);

  if (error != xtc::XtcError::OK || streamOverflow) {
    LOG_ERR(TAG, "Failed to compose page %u grayscale pass %u", pageNum, static_cast<unsigned int>(pass));
    return error == xtc::XtcError::OK ? RenderResult::PageLoadFailed : mapStreamError(error);
  }
  return RenderResult::Success;
}

bool XtcPageRenderer::usesNativeXthLayout(const uint16_t width, const uint16_t height, const size_t planeSize) const {
  return renderer_.getOrientation() == GfxRenderer::Portrait && width == renderer_.getScreenWidth() &&
         height == renderer_.getScreenHeight() && planeSize == renderer_.getBufferSize();
}

void XtcPageRenderer::recoverGrayscaleFailure() {
  renderer_.clearScreen();
  renderer_.cleanupGrayscaleWithFrameBuffer();
}

}  // namespace papyrix
