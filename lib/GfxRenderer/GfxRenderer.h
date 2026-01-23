#pragma once

#include <EInkDisplay.h>
#include <EpdFontFamily.h>

#include <map>
#include <unordered_map>
#include <vector>

#include "Bitmap.h"

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };

  // Logical screen orientation from the perspective of callers
  enum Orientation {
    Portrait,                  // 480x800 logical coordinates (current default)
    LandscapeClockwise,        // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    PortraitInverted,          // 480x800 logical coordinates, inverted
    LandscapeCounterClockwise  // 800x480 logical coordinates, native panel orientation
  };

 private:
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;  // 8KB chunks to allow for non-contiguous memory
  static constexpr size_t BW_BUFFER_NUM_CHUNKS = EInkDisplay::BUFFER_SIZE / BW_BUFFER_CHUNK_SIZE;
  static_assert(BW_BUFFER_CHUNK_SIZE * BW_BUFFER_NUM_CHUNKS == EInkDisplay::BUFFER_SIZE,
                "BW buffer chunking does not line up with display buffer size");

  EInkDisplay& einkDisplay;
  RenderMode renderMode;
  Orientation orientation;
  uint8_t* bwBufferChunks[BW_BUFFER_NUM_CHUNKS] = {nullptr};
  std::map<int, EpdFontFamily> fontMap;

  // Word width cache for performance optimization during EPUB section creation.
  // Key: FNV-1a hash of (fontId, text, style). Value: measured width in pixels.
  mutable std::unordered_map<uint64_t, int16_t> wordWidthCache;

  uint64_t makeWidthCacheKey(int fontId, const char* text, EpdFontFamily::Style style) const {
    // FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    hash ^= static_cast<uint64_t>(fontId);
    hash *= 1099511628211ULL;
    hash ^= static_cast<uint64_t>(style);
    hash *= 1099511628211ULL;
    if (text) {
      while (*text) {
        hash ^= static_cast<uint8_t>(*text++);
        hash *= 1099511628211ULL;
      }
    }
    return hash;
  }

  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, const int* y, bool pixelState,
                  EpdFontFamily::Style style) const;
  void freeBwBufferChunks();
  void rotateCoordinates(int x, int y, int* rotatedX, int* rotatedY) const;

 public:
  explicit GfxRenderer(EInkDisplay& einkDisplay) : einkDisplay(einkDisplay), renderMode(BW), orientation(Portrait) {}
  ~GfxRenderer() = default;

  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  // Setup
  void insertFont(int fontId, EpdFontFamily font);
  void clearWidthCache() { wordWidthCache.clear(); }

  // Orientation control (affects logical width/height and coordinate transforms)
  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  // Screen ops
  int getScreenWidth() const;
  int getScreenHeight() const;
  void displayBuffer(EInkDisplay::RefreshMode refreshMode = EInkDisplay::FAST_REFRESH) const;
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  void displayWindow(int x, int y, int width, int height) const;
  void invertScreen() const;
  void clearScreen(uint8_t color = 0xFF) const;
  void clearArea(int x, int y, int width, int height, uint8_t color = 0xFF) const;

  // Drawing
  void drawPixel(int x, int y, bool state = true) const;
  void drawLine(int x1, int y1, int x2, int y2, bool state = true) const;
  void drawRect(int x, int y, int width, int height, bool state = true) const;
  void fillRect(int x, int y, int width, int height, bool state = true) const;
  void drawImage(const uint8_t bitmap[], int x, int y, int width, int height) const;
  void drawBitmap(const Bitmap& bitmap, int x, int y, int maxWidth, int maxHeight) const;

  // Text
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawCenteredText(int fontId, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawText(int fontId, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getSpaceWidth(int fontId) const;
  int getFontAscenderSize(int fontId) const;
  int getLineHeight(int fontId) const;
  std::string truncatedText(const int fontId, const char* text, const int maxWidth,
                            const EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  // Breaks a single word into chunks that fit within maxWidth, adding "-" where needed
  std::vector<std::string> breakWordWithHyphenation(int fontId, const char* word, int maxWidth,
                                                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  std::vector<std::string> wrapTextWithHyphenation(int fontId, const char* text, int maxWidth, int maxLines,
                                                   EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  bool fontSupportsGrayscale(int fontId) const;

  // UI Components
  void drawButtonHints(int fontId, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                       bool black = true) const;

  // Grayscale functions
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer() const;
  bool storeBwBuffer();  // Returns true if buffer was stored successfully
  void restoreBwBuffer();
  void cleanupGrayscaleWithFrameBuffer() const;

  // Low level functions
  uint8_t* getFrameBuffer() const;
  static size_t getBufferSize();
  void grayscaleRevert() const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;
};
