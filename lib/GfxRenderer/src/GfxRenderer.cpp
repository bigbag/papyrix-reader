#include "GfxRenderer.h"

#include <ArabicShaper.h>
#include <ExternalFont.h>
#include <Logging.h>
#include <ScriptDetector.h>
#include <StreamingEpdFont.h>
#include <ThaiShaper.h>
#include <Utf8.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#define TAG "GFX"

struct TextScriptFlags {
  bool hasArabic = false;
  bool hasThai = false;
  bool hasCjk = false;
};

static inline TextScriptFlags detectTextScripts(const char* text) {
  TextScriptFlags flags;
  if (!text) return flags;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&ptr))) {
    if (!flags.hasArabic && ScriptDetector::isArabicCodepoint(cp))
      flags.hasArabic = true;
    else if (!flags.hasThai && ScriptDetector::isThaiCodepoint(cp))
      flags.hasThai = true;
    else if (!flags.hasCjk && ScriptDetector::isCjkCodepoint(cp))
      flags.hasCjk = true;
    if (flags.hasArabic && flags.hasThai && flags.hasCjk) break;
  }
  return flags;
}

static std::vector<size_t> utf8PrefixBoundaries(const std::string& text) {
  std::vector<size_t> boundaries;
  boundaries.reserve(text.size() + 1);
  boundaries.push_back(0);

  const char* ptr = text.c_str();
  while (*ptr) {
    const char* next = ptr;
    utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&next));
    boundaries.push_back(static_cast<size_t>(next - text.c_str()));
    ptr = next;
  }

  return boundaries;
}

static inline void writeFB(uint8_t* fb, int stride, int physX, int physY, bool state) {
  const int idx = physY * stride + (physX >> 3);
  const uint8_t bit = static_cast<uint8_t>(1 << (7 - (physX & 7)));
  if (state)
    fb[idx] &= ~bit;
  else
    fb[idx] |= bit;
}

static inline bool extractFontPixel(const uint8_t* bitmap, int pixelPos, bool is2Bit, GfxRenderer::RenderMode mode,
                                    bool pixelState, bool& outState) {
  if (is2Bit) {
    const uint8_t byte = bitmap[pixelPos / 4];
    const uint8_t bitIdx = static_cast<uint8_t>((3 - pixelPos % 4) * 2);
    const uint8_t bmpVal = (3 - (byte >> bitIdx)) & 0x3;
    if (mode == GfxRenderer::BW && bmpVal < 3) {
      outState = pixelState;
      return true;
    }
    if (mode == GfxRenderer::GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
      outState = false;
      return true;
    }
    if (mode == GfxRenderer::GRAYSCALE_LSB && bmpVal == 1) {
      outState = false;
      return true;
    }
    return false;
  }
  const uint8_t byte = bitmap[pixelPos / 8];
  if ((byte >> (7 - (pixelPos % 8))) & 1) {
    outState = pixelState;
    return true;
  }
  return false;
}

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) { fontMap.insert({fontId, font}); }

void GfxRenderer::removeFont(const int fontId) {
  fontMap.erase(fontId);
  _streamingFonts.erase(fontId);
  clearWidthCache();
}

bool GfxRenderer::tryResolveExternalFont() const {
  if (_externalFont) return true;
  if (!_externalFontResolver) return false;
  _externalFontResolver(_externalFontResolverCtx);
  // Resolver cleared after use — only triggers once
  _externalFontResolver = nullptr;
  _externalFontResolverCtx = nullptr;
  return _externalFont != nullptr;
}

void GfxRenderer::rotateCoordinates(const int x, const int y, int* rotatedX, int* rotatedY) const {
  const int panelWidth = einkDisplay.getDisplayWidth();
  const int panelHeight = einkDisplay.getDisplayHeight();
  switch (orientation) {
    case Portrait: {
      // Logical portrait → panel landscape; 90° clockwise.
      *rotatedX = y;
      *rotatedY = panelHeight - 1 - x;
      break;
    }
    case LandscapeClockwise: {
      // Logical landscape rotated 180° (swap top/bottom and left/right).
      *rotatedX = panelWidth - 1 - x;
      *rotatedY = panelHeight - 1 - y;
      break;
    }
    case PortraitInverted: {
      // Logical portrait → panel landscape; 90° counter-clockwise.
      *rotatedX = panelWidth - 1 - y;
      *rotatedY = x;
      break;
    }
    case LandscapeCounterClockwise: {
      // Logical landscape aligned with native panel orientation.
      *rotatedX = x;
      *rotatedY = y;
      break;
    }
  }
}

void GfxRenderer::begin() {
  frameBuffer = einkDisplay.getFrameBuffer();
  assert(frameBuffer && "GfxRenderer::begin() called before display.begin()");
}

void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(x, y, &rotatedX, &rotatedY);

  // Bounds checking against physical panel dimensions
  if (rotatedX < 0 || rotatedX >= einkDisplay.getDisplayWidth() || rotatedY < 0 ||
      rotatedY >= einkDisplay.getDisplayHeight()) {
    LOG_ERR(TAG, "!! Outside range (%d, %d) -> (%d, %d)", x, y, rotatedX, rotatedY);
    return;
  }

  // Calculate byte position and bit position
  const uint16_t byteIndex = rotatedY * einkDisplay.getDisplayWidthBytes() + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);  // MSB first

  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;  // Set bit
  }
}

int GfxRenderer::getTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (!text || !*text) return 0;

  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return 0;
  }

  // Trigger lazy loading of deferred font variant (e.g., bold custom font)
  if (style != EpdFontFamily::REGULAR) {
    getStreamingFont(fontId, style);
  }

  // Check cache first (significant speedup during EPUB section creation)
  const uint64_t key = makeWidthCacheKey(fontId, text, style);
  size_t slot = key % MAX_WIDTH_CACHE_SIZE;
  for (size_t i = 0; i < MAX_WIDTH_CACHE_SIZE; i++) {
    if (widthCacheKeys_[slot] == key) {
      return widthCacheValues_[slot];
    }
    if (widthCacheKeys_[slot] == 0) break;  // Empty slot — not in cache
    slot = (slot + 1) % MAX_WIDTH_CACHE_SIZE;
  }

  int w = 0;
  const auto scripts = detectTextScripts(text);
  if (scripts.hasArabic) {
    w = getArabicTextWidth(fontId, text, style);
  } else if (scripts.hasThai) {
    w = getThaiTextWidth(fontId, text, style);
  } else if (isExternalFontAllowed(fontId) &&
             ((_externalFont && _externalFont->isLoaded()) || tryResolveExternalFont())) {
    // Character-by-character: try external font first, then builtin fallback
    const auto& font = it->second;
    const char* ptr = text;
    uint32_t cp;
    while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr)))) {
      const int extWidth = getExternalGlyphWidth(cp);
      if (extWidth > 0) {
        w += extWidth;
      } else {
        const EpdGlyph* glyph = font.getGlyph(cp, style);
        if (glyph) {
          w += glyph->advanceX;
        } else {
          const EpdGlyph* fallback = font.getGlyph('?', style);
          if (fallback) {
            w += fallback->advanceX;
          }
        }
      }
    }
  } else {
    int h = 0;
    it->second.getTextDimensions(text, &w, &h, style);
  }

  // Insert into flat hash table; clear if full
  if (widthCacheCount_ >= MAX_WIDTH_CACHE_SIZE) {
    clearWidthCache();
  }

  slot = key % MAX_WIDTH_CACHE_SIZE;
  for (size_t i = 0; i < MAX_WIDTH_CACHE_SIZE; i++) {
    if (widthCacheKeys_[slot] == 0) {
      widthCacheKeys_[slot] = key;
      widthCacheValues_[slot] = static_cast<int16_t>(w);
      widthCacheCount_++;
      break;
    }
    slot = (slot + 1) % MAX_WIDTH_CACHE_SIZE;
  }

  return w;
}

void GfxRenderer::drawCenteredText(const int fontId, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - getTextWidth(fontId, text, style)) / 2;
  drawText(fontId, x, y, text, black, style);
}

void GfxRenderer::drawText(const int fontId, const int x, const int y, const char* text, const bool black,
                           const EpdFontFamily::Style style) const {
  // cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return;
  }

  // Trigger lazy loading of deferred font variant (e.g., bold custom font)
  if (style != EpdFontFamily::REGULAR) {
    getStreamingFont(fontId, style);
  }

  const auto& font = it->second;

  const auto scripts = detectTextScripts(text);
  if (scripts.hasArabic) {
    drawArabicText(fontId, x, y, text, black, style);
    return;
  }
  if (scripts.hasThai) {
    drawThaiText(fontId, x, y, text, black, style);
    return;
  }

  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;
  int lastBaseX = x;
  int lastBaseAdvance = 0;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* glyph = font.getGlyph(cp, style);
      if (glyph) {
        int combX = lastBaseX + lastBaseAdvance / 2 - glyph->width / 2;
        int combY = yPos - 1;
        renderChar(font, cp, &combX, &combY, black, style, fontId);
      }
    } else {
      lastBaseX = xpos;
      renderChar(font, cp, &xpos, &yPos, black, style, fontId);
      lastBaseAdvance = xpos - lastBaseX;
    }
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state) const {
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);
  int sx = (x1 < x2) ? 1 : -1;
  int sy = (y1 < y2) ? 1 : -1;
  int err = dx - dy;

  while (true) {
    drawPixel(x1, y1, state);

    if (x1 == x2 && y1 == y2) break;

    int e2 = 2 * err;

    if (e2 > -dy) {
      err -= dy;
      x1 += sx;
    }

    if (e2 < dx) {
      err += dx;
      y1 += sy;
    }
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state) const {
  drawLine(x, y, x + width - 1, y, state);
  drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
  drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
  drawLine(x, y, x, y + height - 1, state);
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state) const {
  if (width <= 0 || height <= 0) return;

  int physX, physY, physW, physH;
  switch (orientation) {
    case Portrait:
      physX = y;
      physY = einkDisplay.getDisplayHeight() - 1 - (x + width - 1);
      physW = height;
      physH = width;
      break;
    case LandscapeClockwise:
      physX = einkDisplay.getDisplayWidth() - 1 - (x + width - 1);
      physY = einkDisplay.getDisplayHeight() - 1 - (y + height - 1);
      physW = width;
      physH = height;
      break;
    case PortraitInverted:
      physX = einkDisplay.getDisplayWidth() - 1 - (y + height - 1);
      physY = x;
      physW = height;
      physH = width;
      break;
    case LandscapeCounterClockwise:
    default:
      physX = x;
      physY = y;
      physW = width;
      physH = height;
      break;
  }

  const int dw = static_cast<int>(einkDisplay.getDisplayWidth());
  const int dh = static_cast<int>(einkDisplay.getDisplayHeight());
  if (physX >= dw || physY >= dh || physX + physW <= 0 || physY + physH <= 0) return;

  const int x0 = std::max(physX, 0);
  const int y0 = std::max(physY, 0);
  const int x1 = std::min(physX + physW - 1, dw - 1);
  const int y1 = std::min(physY + physH - 1, dh - 1);

  const int stride = einkDisplay.getDisplayWidthBytes();
  const int byteStart = x0 / 8;
  const int byteEnd = x1 / 8;

  if (byteStart == byteEnd) {
    const uint8_t mask = static_cast<uint8_t>((0xFF >> (x0 & 7)) & (0xFF << (7 - (x1 & 7))));
    for (int row = y0; row <= y1; row++) {
      uint8_t& b = frameBuffer[row * stride + byteStart];
      if (state)
        b &= ~mask;
      else
        b |= mask;
    }
    return;
  }

  const bool hasLeftEdge = (x0 & 7) != 0;
  const bool hasRightEdge = (x1 & 7) != 7;
  const uint8_t leftMask = static_cast<uint8_t>(0xFF >> (x0 & 7));
  const uint8_t rightMask = static_cast<uint8_t>(0xFF << (7 - (x1 & 7)));
  const uint8_t fill = state ? 0x00 : 0xFF;
  const int fullStart = byteStart + (hasLeftEdge ? 1 : 0);
  const int fullEnd = byteEnd - (hasRightEdge ? 1 : 0);
  const int fullCount = fullEnd - fullStart + 1;

  for (int row = y0; row <= y1; row++) {
    uint8_t* rowPtr = &frameBuffer[row * stride];
    if (hasLeftEdge) {
      if (state)
        rowPtr[byteStart] &= ~leftMask;
      else
        rowPtr[byteStart] |= leftMask;
    }
    if (fullCount > 0) {
      memset(&rowPtr[fullStart], fill, fullCount);
    }
    if (hasRightEdge) {
      if (state)
        rowPtr[byteEnd] &= ~rightMask;
      else
        rowPtr[byteEnd] |= rightMask;
    }
  }
}

void GfxRenderer::drawImage(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  // TODO: Rotate bits
  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(x, y, &rotatedX, &rotatedY);
  einkDisplay.drawImage(bitmap, rotatedX, rotatedY, width, height);
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                             const int maxHeight) const {
  float scale = 1.0f;
  bool isScaled = false;
  if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
    isScaled = true;
  }

  // Use pre-allocated row buffers to avoid per-call heap allocation
  // Verify bitmap fits within our pre-allocated buffer sizes
  const size_t outputRowSize = static_cast<size_t>((bitmap.getWidth() + 3) / 4);
  const size_t rowBytesSize = static_cast<size_t>(bitmap.getRowBytes());

  if (!ensureBitmapRowBuffers()) {
    return;
  }

  if (outputRowSize > BITMAP_OUTPUT_ROW_SIZE || rowBytesSize > BITMAP_ROW_BYTES_SIZE) {
    LOG_ERR(TAG, "!! Bitmap too large for pre-allocated buffers (%zu > %zu or %zu > %zu)", outputRowSize,
            BITMAP_OUTPUT_ROW_SIZE, rowBytesSize, BITMAP_ROW_BYTES_SIZE);
    return;
  }

  // Inverse mapping: iterate destination pixels, sample from source.
  // This avoids gaps/overlaps that forward mapping causes when downscaling.
  const int destWidth = isScaled ? static_cast<int>(bitmap.getWidth() * scale) : bitmap.getWidth();
  const int destHeight = isScaled ? static_cast<int>(bitmap.getHeight() * scale) : bitmap.getHeight();
  const float invScale = isScaled ? (1.0f / scale) : 1.0f;

  const int screenW = getScreenWidth();
  const int screenH = getScreenHeight();
  const int dxStart = std::max(0, -x);
  const int dxEnd = std::min(destWidth, screenW - x);
  if (dxStart >= dxEnd) return;

  const int panelW = einkDisplay.getDisplayWidth();
  const int panelH = einkDisplay.getDisplayHeight();
  const int stride = einkDisplay.getDisplayWidthBytes();
  const int bmpW = bitmap.getWidth();

  int lastSrcY = -1;
  for (int destY = 0; destY < destHeight; destY++) {
    const int screenY = bitmap.isTopDown() ? (y + destY) : (y + destHeight - 1 - destY);
    if (screenY < 0 || screenY >= screenH) continue;

    int srcY = isScaled ? static_cast<int>(destY * invScale) : destY;
    if (srcY >= bitmap.getHeight()) srcY = bitmap.getHeight() - 1;

    if (srcY != lastSrcY) {
      if (bitmap.readRow(bitmapOutputRow_, bitmapRowBytes_, srcY) != BmpReaderError::Ok) {
        LOG_ERR(TAG, "Failed to read row %d from bitmap", srcY);
        return;
      }
      lastSrcY = srcY;
    }

#define DRAWBITMAP_INNER(PHYS_X_EXPR, PHYS_Y_EXPR)                                    \
  for (int dx = dxStart; dx < dxEnd; dx++) {                                          \
    int bmpX = isScaled ? static_cast<int>(dx * invScale) : dx;                       \
    if (bmpX >= bmpW) bmpX = bmpW - 1;                                                \
    const uint8_t val = (bitmapOutputRow_[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x3; \
    const int sX = x + dx;                                                            \
    if (renderMode == BW && val < 3) {                                                \
      writeFB(frameBuffer, stride, (PHYS_X_EXPR), (PHYS_Y_EXPR), true);               \
    } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {               \
      writeFB(frameBuffer, stride, (PHYS_X_EXPR), (PHYS_Y_EXPR), false);              \
    } else if (renderMode == GRAYSCALE_LSB && val == 1) {                             \
      writeFB(frameBuffer, stride, (PHYS_X_EXPR), (PHYS_Y_EXPR), false);              \
    }                                                                                 \
  }

    switch (orientation) {
      case LandscapeCounterClockwise:
        DRAWBITMAP_INNER(sX, screenY)
        break;
      case Portrait:
        DRAWBITMAP_INNER(screenY, panelH - 1 - sX)
        break;
      case LandscapeClockwise:
        DRAWBITMAP_INNER(panelW - 1 - sX, panelH - 1 - screenY)
        break;
      case PortraitInverted:
        DRAWBITMAP_INNER(panelW - 1 - screenY, sX)
        break;
    }

#undef DRAWBITMAP_INNER
  }
}

static unsigned long renderStartMs = 0;

void GfxRenderer::clearScreen(const uint8_t color) const {
  renderStartMs = millis();
  einkDisplay.clearScreen(color);
}

void GfxRenderer::clearArea(const int x, const int y, const int width, const int height, const uint8_t color) const {
  if (width <= 0 || height <= 0) {
    return;
  }

  // Rotate logical rectangle to physical coordinates
  int physX, physY, physW, physH;
  switch (orientation) {
    case Portrait:
      physX = y;
      physY = einkDisplay.getDisplayHeight() - 1 - (x + width - 1);
      physW = height;
      physH = width;
      break;
    case LandscapeClockwise:
      physX = einkDisplay.getDisplayWidth() - 1 - (x + width - 1);
      physY = einkDisplay.getDisplayHeight() - 1 - (y + height - 1);
      physW = width;
      physH = height;
      break;
    case PortraitInverted:
      physX = einkDisplay.getDisplayWidth() - 1 - (y + height - 1);
      physY = x;
      physW = height;
      physH = width;
      break;
    case LandscapeCounterClockwise:
    default:
      physX = x;
      physY = y;
      physW = width;
      physH = height;
      break;
  }

  // Validate bounds - region entirely outside display
  if (physX >= static_cast<int>(einkDisplay.getDisplayWidth()) ||
      physY >= static_cast<int>(einkDisplay.getDisplayHeight()) || physX + physW <= 0 || physY + physH <= 0) {
    return;
  }

  // Clamp to display boundaries
  const int x_start = std::max(physX, 0);
  const int y_start = std::max(physY, 0);
  const int x_end = std::min(physX + physW - 1, static_cast<int>(einkDisplay.getDisplayWidth() - 1));
  const int y_end = std::min(physY + physH - 1, static_cast<int>(einkDisplay.getDisplayHeight() - 1));

  // Calculate byte boundaries (8 pixels per byte)
  const int x_byte_start = x_start / 8;
  const int x_byte_end = x_end / 8;
  const int byte_width = x_byte_end - x_byte_start + 1;

  // Clear each row in the region
  for (int row = y_start; row <= y_end; row++) {
    const uint32_t buffer_offset = row * einkDisplay.getDisplayWidthBytes() + x_byte_start;
    memset(&frameBuffer[buffer_offset], color, byte_width);
  }
}

void GfxRenderer::invertScreen() const {
  for (int i = 0; i < einkDisplay.getBufferSize(); i++) {
    frameBuffer[i] = ~frameBuffer[i];
  }
}

void GfxRenderer::displayBufferDriveAll(bool turnOffScreen) const {
  if (renderStartMs > 0) {
    LOG_DBG(TAG, "Render took %lu ms", millis() - renderStartMs);
    renderStartMs = 0;
  }
  einkDisplay.displayBufferDriveAll(turnOffScreen);
}

void GfxRenderer::displayBuffer(const EInkDisplay::RefreshMode refreshMode, bool turnOffScreen) const {
  if (renderStartMs > 0) {
    LOG_DBG(TAG, "Render took %lu ms", millis() - renderStartMs);
    renderStartMs = 0;
  }
  einkDisplay.displayBuffer(refreshMode, turnOffScreen);
}

void GfxRenderer::displayWindow(int x, int y, int width, int height, bool turnOffScreen) const {
  int physX, physY, physW, physH;
  switch (orientation) {
    case Portrait:
      physX = y;
      physY = einkDisplay.getDisplayHeight() - x - width;
      physW = height;
      physH = width;
      break;
    case PortraitInverted:
      physX = einkDisplay.getDisplayWidth() - y - height;
      physY = x;
      physW = height;
      physH = width;
      break;
    case LandscapeClockwise:
      physX = einkDisplay.getDisplayWidth() - x - width;
      physY = einkDisplay.getDisplayHeight() - y - height;
      physW = width;
      physH = height;
      break;
    case LandscapeCounterClockwise:
    default:
      physX = x;
      physY = y;
      physW = width;
      physH = height;
      break;
  }
  // E-ink controller requires x and width to be byte-aligned (multiples of 8 pixels).
  // Expand the window outward to the nearest byte boundaries.
  int alignedEnd = (physX + physW + 7) & ~7;
  physX = physX & ~7;
  physW = alignedEnd - physX;
  einkDisplay.displayWindow(physX, physY, physW, physH, turnOffScreen);
}

std::string GfxRenderer::truncatedText(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  std::string item = text;
  const int itemWidth = getTextWidth(fontId, item.c_str(), style);
  if (itemWidth <= maxWidth || item.length() <= 8) {
    return item;
  }

  const std::vector<size_t> boundaries = utf8PrefixBoundaries(item);
  const auto scripts = detectTextScripts(text);
  const bool nonMonotonic = scripts.hasArabic || scripts.hasThai;

  size_t best = 0;

  if (nonMonotonic) {
    // Arabic/Thai reshaping makes width non-monotonic w.r.t. prefix length — linear scan
    for (size_t i = boundaries.size() - 1; i > 0; --i) {
      std::string candidate = item.substr(0, boundaries[i]);
      candidate.append("...");
      if (getTextWidth(fontId, candidate.c_str(), style) <= maxWidth) {
        best = i;
        break;
      }
    }
  } else {
    size_t left = 0;
    size_t right = boundaries.size() - 1;

    while (left <= right) {
      const size_t mid = left + (right - left) / 2;
      std::string candidate = item.substr(0, boundaries[mid]);
      candidate.append("...");
      const int candidateWidth = getTextWidth(fontId, candidate.c_str(), style);

      if (candidateWidth <= maxWidth) {
        best = mid;
        left = mid + 1;
      } else {
        if (mid == 0) break;
        right = mid - 1;
      }
    }
  }

  if (best == 0) {
    return "...";
  }

  item.resize(boundaries[best]);
  item.append("...");
  return item;
}

std::vector<std::string> GfxRenderer::breakWordWithHyphenation(const int fontId, const char* word, const int maxWidth,
                                                               const EpdFontFamily::Style style) const {
  std::vector<std::string> chunks;
  if (!word || *word == '\0') return chunks;

  std::string remaining = word;
  const auto scripts = detectTextScripts(word);
  const bool nonMonotonic = scripts.hasArabic || scripts.hasThai;

  while (!remaining.empty()) {
    const int remainingWidth = getTextWidth(fontId, remaining.c_str(), style);
    if (remainingWidth <= maxWidth) {
      chunks.push_back(remaining);
      break;
    }

    const std::vector<size_t> boundaries = utf8PrefixBoundaries(remaining);
    size_t best = 0;

    if (nonMonotonic) {
      for (size_t i = boundaries.size() - 1; i > 0; --i) {
        const bool hasTail = boundaries[i] < remaining.size();
        std::string candidate = remaining.substr(0, boundaries[i]);
        if (hasTail) candidate.push_back('-');
        if (getTextWidth(fontId, candidate.c_str(), style) <= maxWidth) {
          best = i;
          break;
        }
      }
    } else {
      size_t left = 1;
      size_t right = boundaries.size() - 1;

      while (left <= right) {
        const size_t mid = left + (right - left) / 2;
        const bool hasTail = boundaries[mid] < remaining.size();
        std::string candidate = remaining.substr(0, boundaries[mid]);
        if (hasTail) {
          candidate.push_back('-');
        }

        const int candidateWidth = getTextWidth(fontId, candidate.c_str(), style);
        if (candidateWidth <= maxWidth) {
          best = mid;
          left = mid + 1;
        } else {
          if (mid == 0) break;
          right = mid - 1;
        }
      }
    }

    if (best == 0) {
      best = 1;
    }

    std::string chunk = remaining.substr(0, boundaries[best]);
    const bool hasTail = boundaries[best] < remaining.size();
    if (hasTail) {
      chunk.push_back('-');
      chunks.push_back(std::move(chunk));
      remaining.erase(0, boundaries[best]);
    } else {
      chunks.push_back(std::move(chunk));
      remaining.clear();
    }
  }
  return chunks;
}

std::vector<std::string> GfxRenderer::wrapTextWithHyphenation(const int fontId, const char* text, const int maxWidth,
                                                              const int maxLines,
                                                              const EpdFontFamily::Style style) const {
  std::vector<std::string> lines;
  if (!text || *text == '\0' || maxLines <= 0) {
    return lines;
  }

  std::string remaining = text;

  while (!remaining.empty() && static_cast<int>(lines.size()) < maxLines) {
    const bool isLastLine = static_cast<int>(lines.size()) == maxLines - 1;

    // Check if remaining text fits on current line
    const int remainingWidth = getTextWidth(fontId, remaining.c_str(), style);
    if (remainingWidth <= maxWidth) {
      lines.push_back(remaining);
      break;
    }

    // Find where to break the line
    std::string currentLine;
    const char* ptr = remaining.c_str();
    const char* lastBreakPoint = nullptr;
    std::string lineAtBreak;

    while (*ptr) {
      // Skip to end of current word
      const char* wordEnd = ptr;
      while (*wordEnd && *wordEnd != ' ') {
        utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&wordEnd));
      }

      // Build line up to this word
      std::string testLine = currentLine;
      if (!testLine.empty()) {
        testLine += ' ';
      }
      testLine.append(ptr, wordEnd - ptr);

      const int testWidth = getTextWidth(fontId, testLine.c_str(), style);

      if (testWidth <= maxWidth) {
        // Word fits, update current line and remember this as potential break point
        currentLine = testLine;
        lastBreakPoint = wordEnd;
        lineAtBreak = currentLine;

        // Move past the word and any spaces
        ptr = wordEnd;
        while (*ptr == ' ') {
          ptr++;
        }
      } else {
        // Word doesn't fit
        if (currentLine.empty()) {
          // Word alone is too long - use helper
          auto wordChunks = breakWordWithHyphenation(fontId, std::string(ptr, wordEnd - ptr).c_str(), maxWidth, style);
          for (size_t i = 0; i < wordChunks.size() && static_cast<int>(lines.size()) < maxLines; i++) {
            lines.push_back(wordChunks[i]);
          }
          // Update remaining to skip past the word
          ptr = wordEnd;
          while (*ptr == ' ') ptr++;
          remaining = ptr;
          break;
        } else if (lastBreakPoint) {
          // Line has content, break at last good point
          lines.push_back(lineAtBreak);
          // Skip spaces after break point
          const char* nextStart = lastBreakPoint;
          while (*nextStart == ' ') {
            nextStart++;
          }
          remaining = nextStart;
          break;
        }
      }

      if (*ptr == '\0') {
        // Reached end of text
        if (!currentLine.empty()) {
          lines.push_back(currentLine);
        }
        remaining.clear();
        break;
      }
    }

    // Handle last line truncation
    if (isLastLine && !remaining.empty() && static_cast<int>(lines.size()) == maxLines) {
      // Last line but text remains - truncate with "..."
      std::string& lastLine = lines.back();
      lastLine = truncatedText(fontId, lastLine.c_str(), maxWidth, style);
    }
  }

  // If we have remaining text and hit maxLines, truncate the last line
  if (!remaining.empty() && static_cast<int>(lines.size()) == maxLines) {
    std::string& lastLine = lines.back();
    // Append remaining text and truncate
    if (getTextWidth(fontId, lastLine.c_str(), style) < maxWidth) {
      std::string combined = lastLine + " " + remaining;
      lastLine = truncatedText(fontId, combined.c_str(), maxWidth, style);
    } else {
      lastLine = truncatedText(fontId, lastLine.c_str(), maxWidth, style);
    }
  }

  return lines;
}

// Note: Internal driver treats screen in command orientation; this library exposes a logical orientation
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 480px wide in portrait logical coordinates
      return einkDisplay.getDisplayHeight();
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 800px wide in landscape logical coordinates
      return einkDisplay.getDisplayWidth();
  }
  return einkDisplay.getDisplayHeight();
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 800px tall in portrait logical coordinates
      return einkDisplay.getDisplayWidth();
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 480px tall in landscape logical coordinates
      return einkDisplay.getDisplayHeight();
  }
  return einkDisplay.getDisplayWidth();
}

int GfxRenderer::getSpaceWidth(const int fontId) const {
  auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return 0;
  }

  const EpdGlyph* glyph = it->second.getGlyph(' ', EpdFontFamily::REGULAR);
  return glyph ? glyph->advanceX : 0;
}

int GfxRenderer::getFontAscenderSize(const int fontId) const {
  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return 0;
  }

  return it->second.getData(EpdFontFamily::REGULAR)->ascender;
}

int GfxRenderer::getLineHeight(const int fontId) const {
  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return 0;
  }

  return it->second.getData(EpdFontFamily::REGULAR)->advanceY;
}

int GfxRenderer::getEffectiveLineHeight(const int fontId) const {
  int h = getLineHeight(fontId);
  if (isExternalFontAllowed(fontId) && _externalFont && _externalFont->isLoaded()) {
    int extH = _externalFont->getCharHeight() + 2;
    if (extH > h) h = extH;
  }
  return h;
}

bool GfxRenderer::fontSupportsGrayscale(const int fontId) const {
  auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    return false;
  }
  const EpdFontData* data = it->second.getData();
  return data != nullptr && data->is2Bit;
}

void GfxRenderer::drawButtonHints(const int fontId, const char* btn1, const char* btn2, const char* btn3,
                                  const char* btn4, const bool black) const {
  const int pageHeight = getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = 46;
  constexpr int buttonY = 50;      // Distance from bottom
  constexpr int textYOffset = 10;  // Distance from top of button to text baseline
  constexpr int buttonPositions[] = {25, 130, 245, 350};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    // Only draw if the label is non-empty
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, black);
      const int textWidth = getTextWidth(fontId, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      drawText(fontId, textX, pageHeight - buttonY + textYOffset, labels[i], black);
    }
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return frameBuffer; }

size_t GfxRenderer::getBufferSize() const { return einkDisplay.getBufferSize(); }

void GfxRenderer::grayscaleRevert() const { einkDisplay.grayscaleRevert(); }

void GfxRenderer::copyGrayscaleLsbBuffers() const { einkDisplay.copyGrayscaleLsbBuffers(frameBuffer); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { einkDisplay.copyGrayscaleMsbBuffers(frameBuffer); }

void GfxRenderer::displayGrayBuffer(bool turnOffScreen) const { einkDisplay.displayGrayBuffer(turnOffScreen); }

void GfxRenderer::freeBwBufferChunks() {
  for (auto& bwBufferChunk : bwBufferChunks) {
    if (bwBufferChunk) {
      free(bwBufferChunk);
      bwBufferChunk = nullptr;
    }
  }
}

/**
 * This should be called before grayscale buffers are populated.
 * A `restoreBwBuffer` call should always follow the grayscale render if this method was called.
 * Uses chunked allocation to avoid needing 48KB of contiguous memory.
 * Returns true if buffer was stored successfully, false if allocation failed.
 */
bool GfxRenderer::storeBwBuffer() {
  const size_t bufferSize = einkDisplay.getBufferSize();

  // Allocate and copy each chunk. The last chunk may be partial when
  // bufferSize isn't a multiple of BW_BUFFER_CHUNK_SIZE (X3: 52272 / 8000 → 7 chunks).
  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    if (bwBufferChunks[i]) {
      LOG_ERR(TAG, "!! BW buffer chunk %zu already stored - this is likely a bug, freeing chunk", i);
      free(bwBufferChunks[i]);
      bwBufferChunks[i] = nullptr;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    if (offset >= bufferSize) {
      // Slot beyond the runtime panel size — leave nullptr.
      continue;
    }
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, bufferSize - offset);
    bwBufferChunks[i] = static_cast<uint8_t*>(malloc(chunkSize));

    if (!bwBufferChunks[i]) {
      LOG_ERR(TAG, "!! Failed to allocate BW buffer chunk %zu (%zu bytes)", i, chunkSize);
      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, chunkSize);
  }

  LOG_DBG(TAG, "Stored BW buffer (%zu bytes) in chunks of %zu", bufferSize, BW_BUFFER_CHUNK_SIZE);
  return true;
}

/**
 * This can only be called if `storeBwBuffer` was called prior to the grayscale render.
 * It should be called to restore the BW buffer state after grayscale rendering is complete.
 * Uses chunked restoration to match chunked storage.
 */
void GfxRenderer::restoreBwBuffer() {
  const size_t bufferSize = einkDisplay.getBufferSize();

  // Validate that every required chunk (slots within bufferSize) is present.
  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    if (offset >= bufferSize) break;
    if (!bwBufferChunks[i]) {
      LOG_ERR(TAG, "!! BW buffer chunks not stored - this is likely a bug");
      freeBwBufferChunks();
      return;
    }
  }

  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    if (offset >= bufferSize) break;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, bufferSize - offset);
    memcpy(frameBuffer + offset, bwBufferChunks[i], chunkSize);
  }

  einkDisplay.cleanupGrayscaleBuffers(frameBuffer);

  freeBwBufferChunks();
  LOG_DBG(TAG, "Restored and freed BW buffer chunks");
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const { einkDisplay.cleanupGrayscaleBuffers(frameBuffer); }

void GfxRenderer::renderChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                             const bool pixelState, const EpdFontFamily::Style style, const int fontId) const {
  // Try external font first — covers CJK and optionally Latin from .bin fonts
  if (isExternalFontAllowed(fontId) && (_externalFont || tryResolveExternalFont()) && _externalFont->isLoaded()) {
    const uint8_t* extBitmap = _externalFont->getGlyph(cp);
    if (extBitmap) {
      renderExternalGlyph(cp, x, *y - fontFamily.getData(EpdFontFamily::REGULAR)->ascender, pixelState, extBitmap);
      return;
    }
  }

  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    // For whitespace characters missing from font, advance by space width instead of rendering '?'
    if (cp == 0x2002 || cp == 0x2003 || cp == 0x00A0) {  // EN SPACE, EM SPACE, NBSP
      const EpdGlyph* spaceGlyph = fontFamily.getGlyph(' ', style);
      if (spaceGlyph) {
        *x += spaceGlyph->advanceX;
        if (cp == 0x2003) *x += spaceGlyph->advanceX;  // EM SPACE = 2x width
        return;
      }
    }
    glyph = fontFamily.getGlyph('?', style);
  }

  // no glyph?
  if (!glyph) {
    LOG_ERR(TAG, "No glyph for codepoint %d", cp);
    return;
  }

  const int is2Bit = fontFamily.getData(style)->is2Bit;
  const uint32_t offset = glyph->dataOffset;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;

  // Bitmap lookup bypasses getStreamingFont() (no lazy resolver) for performance.
  // Font variants are already resolved during layout (word width measurement).
  const uint8_t* bitmap = nullptr;
  auto streamingIt = _streamingFonts.find(fontId);
  if (streamingIt != _streamingFonts.end()) {
    int idx = EpdFontFamily::externalStyleIndex(style);
    StreamingEpdFont* sf = streamingIt->second[idx];
    if (!sf) sf = streamingIt->second[EpdFontFamily::REGULAR];
    if (sf) {
      bitmap = sf->getGlyphBitmap(glyph);
    }
  }
  if (!bitmap && fontFamily.getData(style)->bitmap) {
    // Fall back to standard EpdFont bitmap access
    bitmap = &fontFamily.getData(style)->bitmap[offset];
  }

  if (bitmap != nullptr) {
    const int screenW = getScreenWidth();
    const int screenH = getScreenHeight();
    const int logLeft = *x + left;
    const int logTop = *y - glyph->top;

    const int gxStart = std::max(0, -logLeft);
    const int gxEnd = std::min(static_cast<int>(width), screenW - logLeft);
    const int gyStart = std::max(0, -logTop);
    const int gyEnd = std::min(static_cast<int>(height), screenH - logTop);

    if (gxStart < gxEnd && gyStart < gyEnd) {
      const int panelW = einkDisplay.getDisplayWidth();
      const int panelH = einkDisplay.getDisplayHeight();
      const int stride = einkDisplay.getDisplayWidthBytes();

#define RENDERCHAR_INNER(PHYS_X_EXPR, PHYS_Y_EXPR)                                                  \
  for (int gy = gyStart; gy < gyEnd; gy++) {                                                        \
    const int sY = logTop + gy;                                                                     \
    for (int gx = gxStart; gx < gxEnd; gx++) {                                                      \
      bool st;                                                                                      \
      if (!extractFontPixel(bitmap, gy * width + gx, is2Bit, renderMode, pixelState, st)) continue; \
      const int sX = logLeft + gx;                                                                  \
      writeFB(frameBuffer, stride, (PHYS_X_EXPR), (PHYS_Y_EXPR), st);                               \
    }                                                                                               \
  }

      switch (orientation) {
        case LandscapeCounterClockwise:
          RENDERCHAR_INNER(sX, sY)
          break;
        case Portrait:
          RENDERCHAR_INNER(sY, panelH - 1 - sX)
          break;
        case LandscapeClockwise:
          RENDERCHAR_INNER(panelW - 1 - sX, panelH - 1 - sY)
          break;
        case PortraitInverted:
          RENDERCHAR_INNER(panelW - 1 - sY, sX)
          break;
      }

#undef RENDERCHAR_INNER
    }
  }

  if (!utf8IsCombiningMark(cp)) {
    *x += glyph->advanceX;
  }
}

void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}

bool GfxRenderer::ensureBitmapRowBuffers() const {
  if (bitmapOutputRow_ && bitmapRowBytes_) {
    return true;
  }

  bitmapOutputRow_ = static_cast<uint8_t*>(malloc(BITMAP_OUTPUT_ROW_SIZE));
  bitmapRowBytes_ = static_cast<uint8_t*>(malloc(BITMAP_ROW_BYTES_SIZE));

  if (!bitmapOutputRow_ || !bitmapRowBytes_) {
    LOG_ERR(TAG, "!! Failed to allocate bitmap row buffers (%zu + %zu bytes)", BITMAP_OUTPUT_ROW_SIZE,
            BITMAP_ROW_BYTES_SIZE);
    if (bitmapOutputRow_) {
      free(bitmapOutputRow_);
      bitmapOutputRow_ = nullptr;
    }
    if (bitmapRowBytes_) {
      free(bitmapRowBytes_);
      bitmapRowBytes_ = nullptr;
    }
    return false;
  }
  return true;
}

void GfxRenderer::freeBitmapRowBuffers() {
  if (bitmapOutputRow_) {
    free(bitmapOutputRow_);
    bitmapOutputRow_ = nullptr;
  }
  if (bitmapRowBytes_) {
    free(bitmapRowBytes_);
    bitmapRowBytes_ = nullptr;
  }
}

void GfxRenderer::renderExternalGlyph(const uint32_t cp, int* x, const int y, const bool pixelState,
                                      const uint8_t* bitmap) const {
  if (!_externalFont || !_externalFont->isLoaded()) {
    return;
  }

  if (!bitmap) bitmap = _externalFont->getGlyph(cp);
  if (!bitmap) {
    // Glyph not found - advance by 1/3 char width as fallback
    *x += _externalFont->getCharWidth() / 3;
    return;
  }

  uint8_t minX = 0;
  uint8_t advanceX = 0;
  if (!_externalFont->getGlyphMetrics(cp, &minX, &advanceX)) {
    advanceX = _externalFont->getCharWidth();
  }

  const int w = _externalFont->getCharWidth();
  const int h = _externalFont->getCharHeight();
  const int bytesPerRow = _externalFont->getBytesPerRow();
  const int screenW = getScreenWidth();
  const int screenH = getScreenHeight();
  const int visibleGlyphW = w - minX;

  const int logLeft = *x;
  const int logTop = y;
  const int gxStart = std::max(0, -logLeft);
  const int gxEnd = std::min(visibleGlyphW, screenW - logLeft);
  const int gyStart = std::max(0, -logTop);
  const int gyEnd = std::min(h, screenH - logTop);

  if (gxStart < gxEnd && gyStart < gyEnd) {
    const int panelW = einkDisplay.getDisplayWidth();
    const int panelH = einkDisplay.getDisplayHeight();
    const int stride = einkDisplay.getDisplayWidthBytes();

#define EXTGLYPH_INNER(PHYS_X_EXPR, PHYS_Y_EXPR)                                \
  for (int gy = gyStart; gy < gyEnd; gy++) {                                    \
    const int sY = logTop + gy;                                                 \
    for (int gx = gxStart; gx < gxEnd; gx++) {                                  \
      const int glyphX = gx + minX;                                             \
      const int byteIdx = gy * bytesPerRow + (glyphX / 8);                      \
      const int bitIdx = 7 - (glyphX % 8);                                      \
      if ((bitmap[byteIdx] >> bitIdx) & 1) {                                    \
        const int sX = logLeft + gx;                                            \
        writeFB(frameBuffer, stride, (PHYS_X_EXPR), (PHYS_Y_EXPR), pixelState); \
      }                                                                         \
    }                                                                           \
  }

    switch (orientation) {
      case LandscapeCounterClockwise:
        EXTGLYPH_INNER(sX, sY)
        break;
      case Portrait:
        EXTGLYPH_INNER(sY, panelH - 1 - sX)
        break;
      case LandscapeClockwise:
        EXTGLYPH_INNER(panelW - 1 - sX, panelH - 1 - sY)
        break;
      case PortraitInverted:
        EXTGLYPH_INNER(panelW - 1 - sY, sX)
        break;
    }

#undef EXTGLYPH_INNER
  }

  *x += advanceX;
}

int GfxRenderer::getExternalGlyphWidth(const uint32_t cp) const {
  if (!_externalFont && ScriptDetector::isCjkCodepoint(cp)) {
    tryResolveExternalFont();
  }
  if (!_externalFont || !_externalFont->isLoaded()) {
    return 0;
  }

  // Ensure glyph is loaded to get metrics; return 0 if not found
  // so caller falls back to builtin font width
  if (!_externalFont->getGlyph(cp)) {
    return 0;
  }

  uint8_t minX = 0;
  uint8_t advanceX = _externalFont->getCharWidth();
  if (_externalFont->getGlyphMetrics(cp, &minX, &advanceX)) {
    return advanceX;
  }

  // Fallback to full character width
  return _externalFont->getCharWidth();
}

// ============================================================================
// Thai Text Rendering
// ============================================================================

int GfxRenderer::getThaiTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (text == nullptr || *text == '\0') {
    return 0;
  }

  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return 0;
  }

  const auto& font = it->second;
  int totalWidth = 0;

  // Build clusters and sum their widths
  auto clusters = ThaiShaper::ThaiClusterBuilder::buildClusters(text);

  for (const auto& cluster : clusters) {
    for (const auto& glyph : cluster.glyphs) {
      if (!glyph.zeroAdvance) {
        const EpdGlyph* glyphData = font.getGlyph(glyph.codepoint, style);
        if (!glyphData) {
          glyphData = font.getGlyph('?', style);
        }
        if (glyphData) {
          totalWidth += glyphData->advanceX;
        }
      }
    }
  }

  return totalWidth;
}

void GfxRenderer::drawThaiText(const int fontId, const int x, const int y, const char* text, const bool black,
                               const EpdFontFamily::Style style) const {
  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return;
  }

  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;

  const auto& font = it->second;

  // Build Thai clusters from the text
  auto clusters = ThaiShaper::ThaiClusterBuilder::buildClusters(text);

  // Render each cluster
  for (const auto& cluster : clusters) {
    renderThaiCluster(font, cluster, &xpos, yPos, black, style, fontId);
  }
}

void GfxRenderer::renderThaiCluster(const EpdFontFamily& fontFamily, const ThaiShaper::ThaiCluster& cluster, int* x,
                                    const int y, const bool pixelState, const EpdFontFamily::Style style,
                                    const int fontId) const {
  const EpdFontData* fontData = fontFamily.getData(style);
  if (!fontData) {
    return;
  }

  // Scale factor for stacked marks (tone mark above vowel)
  // 26px is the reference font height used for Thai glyph offset calculations
  const int fontHeight = fontData->advanceY;
  const float yScale = fontHeight / 26.0f;

  int baseX = *x;  // Store base position for combining marks

  for (const auto& glyph : cluster.glyphs) {
    const EpdGlyph* glyphData = fontFamily.getGlyph(glyph.codepoint, style);

    if (!glyphData) {
      glyphData = fontFamily.getGlyph('?', style);
    }
    if (!glyphData) {
      continue;
    }

    const int is2Bit = fontData->is2Bit;
    const uint32_t offset = glyphData->dataOffset;
    const uint8_t width = glyphData->width;
    const uint8_t height = glyphData->height;
    const int left = glyphData->left;

    // Calculate x position for this glyph
    int glyphX;
    if (glyph.zeroAdvance) {
      // Combining mark: position relative to base consonant
      glyphX = baseX + glyph.xOffset;
    } else {
      // Normal glyph: position at current cursor
      glyphX = *x + glyph.xOffset;
    }

    // Calculate y offset - only apply scaling for stacked marks
    int yOffset = 0;
    if (glyph.yOffset < -2) {
      yOffset = static_cast<int>(glyph.yOffset * yScale);
    }
    const int glyphY = y + yOffset;

    if (fontData->bitmap == nullptr) {
      continue;
    }
    const uint8_t* bitmap = &fontData->bitmap[offset];

    const int screenHeight = getScreenHeight();
    const int screenWidth = getScreenWidth();

    for (int bitmapY = 0; bitmapY < height; bitmapY++) {
      const int screenY = glyphY - glyphData->top + bitmapY;
      if (screenY < 0 || screenY >= screenHeight) continue;

      for (int bitmapX = 0; bitmapX < width; bitmapX++) {
        const int pixelPosition = bitmapY * width + bitmapX;
        const int screenX = glyphX + left + bitmapX;
        if (screenX < 0 || screenX >= screenWidth) continue;

        if (is2Bit) {
          const uint8_t byte = bitmap[pixelPosition / 4];
          const uint8_t bit_index = (3 - pixelPosition % 4) * 2;
          const uint8_t bmpVal = 3 - (byte >> bit_index) & 0x3;

          if (renderMode == BW && bmpVal < 3) {
            drawPixel(screenX, screenY, pixelState);
          } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
            drawPixel(screenX, screenY, false);
          }
        } else {
          const uint8_t byte = bitmap[pixelPosition / 8];
          const uint8_t bit_index = 7 - (pixelPosition % 8);

          if ((byte >> bit_index) & 1) {
            drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }

    // Track advance for non-combining glyphs
    if (!glyph.zeroAdvance) {
      baseX = *x + glyphData->advanceX;
      *x += glyphData->advanceX;
    }
  }
}

// ============================================================================
// Arabic Text Rendering
// ============================================================================

int GfxRenderer::getArabicTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (text == nullptr || *text == '\0') return 0;

  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return 0;
  }

  const auto& font = it->second;
  int totalWidth = 0;

  auto shaped = ArabicShaper::shapeText(text);

  for (const auto cp : shaped) {
    const EpdGlyph* glyphData = font.getGlyph(cp, style);
    if (!glyphData) {
      glyphData = font.getGlyph('?', style);
    }
    if (glyphData) {
      totalWidth += glyphData->advanceX;
    }
  }

  return totalWidth;
}

void GfxRenderer::drawArabicText(const int fontId, const int x, const int y, const char* text, const bool black,
                                 const EpdFontFamily::Style style) const {
  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) {
    LOG_ERR(TAG, "Font %d not found", fontId);
    return;
  }

  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;

  const auto& font = it->second;
  auto shaped = ArabicShaper::shapeText(text);

  // Render each shaped codepoint (already in visual LTR order)
  for (const auto cp : shaped) {
    renderChar(font, cp, &xpos, &yPos, black, style, fontId);
  }
}
