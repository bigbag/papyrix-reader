#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

class GfxRenderer;

namespace xtc {
class XtcParser;
}

namespace papyrix {

// XtcPageRenderer - Renders XTC/XTCH binary page data to GfxRenderer
// Supports 1-bit (B&W) and 2-bit (4-level grayscale) formats
class XtcPageRenderer {
 public:
  enum class RenderResult { Success, EndOfBook, InvalidDimensions, AllocationFailed, PageLoadFailed };
  enum class RefreshRequest { Cadenced, GrayscaleBase };
  using RefreshCallback = std::function<void(RefreshRequest)>;

  explicit XtcPageRenderer(GfxRenderer& renderer);

  RenderResult render(xtc::XtcParser& parser, uint32_t pageNum, const RefreshCallback& refreshCallback);

 private:
  enum class GrayscalePass : uint8_t { Base, Lsb, Msb };

  GfxRenderer& renderer_;

  RenderResult render1Bit(xtc::XtcParser& parser, uint32_t pageNum, uint16_t width, uint16_t height,
                          const RefreshCallback& refreshCallback);
  RenderResult render2Bit(xtc::XtcParser& parser, uint32_t pageNum, uint16_t width, uint16_t height,
                          const RefreshCallback& refreshCallback);
  RenderResult compose2BitPass(xtc::XtcParser& parser, uint32_t pageNum, uint16_t width, uint16_t height,
                               GrayscalePass pass);
  bool usesNativeXthLayout(uint16_t width, uint16_t height, size_t planeSize) const;
  void recoverGrayscaleFailure();
};

}  // namespace papyrix
