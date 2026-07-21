#pragma once

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
  // Result of render operation
  enum class RenderResult { Success, EndOfBook, InvalidDimensions, AllocationFailed, PageLoadFailed };
  enum class RefreshRequest { Cadenced, GrayscaleBase };
  using RefreshCallback = std::function<void(RefreshRequest)>;

  explicit XtcPageRenderer(GfxRenderer& renderer);

  // Render a page from the parser
  RenderResult render(xtc::XtcParser& parser, uint32_t pageNum, const RefreshCallback& refreshCallback);

 private:
  GfxRenderer& renderer_;

  // Render 1-bit B&W page (standard XTC)
  void render1Bit(const uint8_t* buffer, uint16_t width, uint16_t height);
};

}  // namespace papyrix
