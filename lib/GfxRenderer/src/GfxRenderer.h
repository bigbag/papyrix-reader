#pragma once

#include <EInkDisplay.h>
#include <EpdFontFamily.h>
#include <ThaiCluster.h>

#include <array>
#include <map>
#include <string>
#include <vector>

#include "Bitmap.h"

// Forward declaration for external CJK font support
class ExternalFont;
// Forward declaration for streaming font support
class StreamingEpdFont;

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
  uint8_t* frameBuffer = nullptr;
  uint8_t* bwBufferChunks[BW_BUFFER_NUM_CHUNKS] = {nullptr};
  std::map<int, EpdFontFamily> fontMap;
  // Streaming fonts: [fontId] -> array of [REGULAR, BOLD] (external fonts have no italic)
  // Mutable: getStreamingFont may trigger lazy loading of bold variant via resolver
  mutable std::map<int, std::array<StreamingEpdFont*, EpdFontFamily::kExternalStyleCount>> _streamingFonts;
  ExternalFont* _externalFont = nullptr;

  // Lazy font style resolver: called when a streaming font variant (bold/italic) is
  // requested but not yet loaded. The callback should load the variant and call
  // setStreamingFont() + updateFontFamily() to register it.
  using FontStyleResolver = void (*)(void* ctx, int fontId, int styleIdx);
  mutable FontStyleResolver _fontStyleResolver = nullptr;
  mutable void* _fontStyleResolverCtx = nullptr;

  // Lazy external font resolver: called on first CJK codepoint encounter when
  // _externalFont is nullptr. The callback should call setExternalFont() to register it.
  using ExternalFontResolver = void (*)(void* ctx);
  mutable ExternalFontResolver _externalFontResolver = nullptr;
  mutable void* _externalFontResolverCtx = nullptr;

  // Pre-allocated row buffers for bitmap rendering (reduces heap fragmentation)
  // Sized for max screen dimension (800 pixels): outputRow = 800/4 = 200 bytes, rowBytes = 800*3 = 2400 bytes (24bpp)
  static constexpr size_t BITMAP_OUTPUT_ROW_SIZE = (EInkDisplay::DISPLAY_WIDTH + 3) / 4;
  static constexpr size_t BITMAP_ROW_BYTES_SIZE = EInkDisplay::DISPLAY_WIDTH * 3;  // 24-bit max
  uint8_t* bitmapOutputRow_ = nullptr;
  uint8_t* bitmapRowBytes_ = nullptr;
  void allocateBitmapRowBuffers();
  void freeBitmapRowBuffers();

  // Word width cache: open-addressing flat hash table for O(1) lookup.
  // Key: FNV-1a hash of (fontId, text, style). Value: measured width in pixels.
  // Uses ~2.6KB vs ~12KB for std::unordered_map with 256 entries.
  static constexpr size_t MAX_WIDTH_CACHE_SIZE = 256;
  mutable uint64_t widthCacheKeys_[MAX_WIDTH_CACHE_SIZE] = {};  // 0 = empty sentinel
  mutable int16_t widthCacheValues_[MAX_WIDTH_CACHE_SIZE] = {};
  mutable uint16_t widthCacheCount_ = 0;

  static uint64_t makeWidthCacheKey(int fontId, const char* text, EpdFontFamily::Style style) {
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
    // Ensure non-zero (0 is our empty sentinel)
    return hash == 0 ? 1 : hash;
  }

  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, const int* y, bool pixelState,
                  EpdFontFamily::Style style, int fontId) const;
  void renderThaiCluster(const EpdFontFamily& fontFamily, const ThaiShaper::ThaiCluster& cluster, int* x, int y,
                         bool pixelState, EpdFontFamily::Style style, int fontId) const;
  void renderExternalGlyph(uint32_t cp, int* x, int y, bool pixelState) const;
  int getExternalGlyphWidth(uint32_t cp) const;
  bool tryResolveExternalFont() const;
  void freeBwBufferChunks();

 public:
  explicit GfxRenderer(EInkDisplay& einkDisplay) : einkDisplay(einkDisplay), renderMode(BW), orientation(Portrait) {
    allocateBitmapRowBuffers();
  }
  ~GfxRenderer() { freeBitmapRowBuffers(); }

  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  // Setup
  void begin();
  void insertFont(int fontId, EpdFontFamily font);
  void removeFont(int fontId);
  void clearWidthCache() const {
    memset(widthCacheKeys_, 0, sizeof(widthCacheKeys_));
    widthCacheCount_ = 0;
  }
  void setExternalFont(ExternalFont* font) { _externalFont = font; }
  ExternalFont* getExternalFont() const { return _externalFont; }

  void setFontStyleResolver(FontStyleResolver resolver, void* ctx) {
    _fontStyleResolver = resolver;
    _fontStyleResolverCtx = ctx;
  }
  void setExternalFontResolver(ExternalFontResolver resolver, void* ctx) {
    _externalFontResolver = resolver;
    _externalFontResolverCtx = ctx;
  }
  void updateFontFamily(int fontId, EpdFontFamily::Style style, const EpdFont* font) {
    auto it = fontMap.find(fontId);
    if (it != fontMap.end()) {
      it->second.setFont(style, font);
    }
  }

  void setStreamingFont(int fontId, EpdFontFamily::Style style, StreamingEpdFont* font) {
    _streamingFonts[fontId][EpdFontFamily::externalStyleIndex(style)] = font;
  }
  void setStreamingFont(int fontId, StreamingEpdFont* font) { _streamingFonts[fontId][EpdFontFamily::REGULAR] = font; }
  void removeStreamingFont(int fontId) { _streamingFonts.erase(fontId); }
  // NOTE: May trigger lazy font loading (SD I/O + allocation) on first access to bold/italic.
  // Thread safety: caller must have exclusive renderer access (ownership model).
  StreamingEpdFont* getStreamingFont(int fontId, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = _streamingFonts.find(fontId);
    if (it == _streamingFonts.end()) return nullptr;
    int idx = EpdFontFamily::externalStyleIndex(style);
    StreamingEpdFont* sf = it->second[idx];
    if (!sf && idx != EpdFontFamily::REGULAR && _fontStyleResolver) {
      _fontStyleResolver(_fontStyleResolverCtx, fontId, idx);
      sf = it->second[idx];
    }
    return sf ? sf : it->second[EpdFontFamily::REGULAR];
  }

  // Orientation control (affects logical width/height and coordinate transforms)
  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  // Screen ops
  int getScreenWidth() const;
  int getScreenHeight() const;
  void displayBuffer(EInkDisplay::RefreshMode refreshMode = EInkDisplay::FAST_REFRESH,
                     bool turnOffScreen = false) const;
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  void displayWindow(int x, int y, int width, int height, bool turnOffScreen = false) const;
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

  // Thai text rendering
  int getThaiTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawThaiText(int fontId, int x, int y, const char* text, bool black = true,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // Arabic text rendering
  int getArabicTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void drawArabicText(int fontId, int x, int y, const char* text, bool black = true,
                      EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

  // UI Components
  void drawButtonHints(int fontId, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                       bool black = true) const;

  // Grayscale functions
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  void copyGrayscaleLsbBuffers() const;
  void copyGrayscaleMsbBuffers() const;
  void displayGrayBuffer(bool turnOffScreen = false) const;
  bool storeBwBuffer();  // Returns true if buffer was stored successfully
  void restoreBwBuffer();
  void cleanupGrayscaleWithFrameBuffer() const;

  // Low level functions
  uint8_t* getFrameBuffer() const;
  static size_t getBufferSize();
  void grayscaleRevert() const;
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const;
};
