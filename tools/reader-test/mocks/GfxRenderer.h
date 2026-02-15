#pragma once

#include <EInkDisplay.h>
#include <EpdFontFamily.h>
#include <ThaiCluster.h>
#include <Utf8.h>

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "Bitmap.h"

class ExternalFont;
class StreamingEpdFont;

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

 private:
  EInkDisplay& einkDisplay;
  RenderMode renderMode;
  Orientation orientation;
  std::map<int, EpdFontFamily> fontMap;
  mutable std::map<int, std::array<StreamingEpdFont*, 3>> _streamingFonts;
  ExternalFont* _externalFont = nullptr;
  using FontStyleResolver = void (*)(void* ctx, int fontId, int styleIdx);
  mutable FontStyleResolver _fontStyleResolver = nullptr;
  mutable void* _fontStyleResolverCtx = nullptr;
  static constexpr size_t MAX_WIDTH_CACHE_SIZE = 256;
  mutable std::unordered_map<uint64_t, int16_t> wordWidthCache;
  static uint8_t frameBuffer_[EInkDisplay::BUFFER_SIZE];

 public:
  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  explicit GfxRenderer(EInkDisplay& einkDisplay) : einkDisplay(einkDisplay), renderMode(BW), orientation(Portrait) {}
  ~GfxRenderer() = default;

  // Setup
  void begin() {}
  void insertFont(int fontId, EpdFontFamily font) { fontMap.emplace(fontId, font); }
  void removeFont(int fontId) { fontMap.erase(fontId); }
  void clearWidthCache() { wordWidthCache.clear(); }
  void setExternalFont(ExternalFont* font) { _externalFont = font; }
  ExternalFont* getExternalFont() const { return _externalFont; }

  void setFontStyleResolver(FontStyleResolver resolver, void* ctx) {
    _fontStyleResolver = resolver;
    _fontStyleResolverCtx = ctx;
  }
  void updateFontFamily(int fontId, EpdFontFamily::Style style, const EpdFont* font) {
    auto it = fontMap.find(fontId);
    if (it != fontMap.end()) it->second.setFont(style, font);
  }

  void setStreamingFont(int fontId, EpdFontFamily::Style style, StreamingEpdFont* font) {
    int idx = (style == EpdFontFamily::BOLD_ITALIC) ? EpdFontFamily::BOLD : style;
    _streamingFonts[fontId][idx] = font;
  }
  void setStreamingFont(int fontId, StreamingEpdFont* font) { _streamingFonts[fontId][EpdFontFamily::REGULAR] = font; }
  void removeStreamingFont(int fontId) { _streamingFonts.erase(fontId); }
  StreamingEpdFont* getStreamingFont(int fontId, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = _streamingFonts.find(fontId);
    if (it == _streamingFonts.end()) return nullptr;
    int idx = (style == EpdFontFamily::BOLD_ITALIC) ? EpdFontFamily::BOLD : style;
    StreamingEpdFont* sf = it->second[idx];
    if (!sf && idx != EpdFontFamily::REGULAR && _fontStyleResolver) {
      _fontStyleResolver(_fontStyleResolverCtx, fontId, idx);
      sf = it->second[idx];
    }
    return sf ? sf : it->second[EpdFontFamily::REGULAR];
  }

  void setOrientation(const Orientation o) { orientation = o; }
  Orientation getOrientation() const { return orientation; }

  // Screen ops - fixed metrics
  int getScreenWidth() const {
    return (orientation == Portrait || orientation == PortraitInverted) ? EInkDisplay::DISPLAY_HEIGHT
                                                                       : EInkDisplay::DISPLAY_WIDTH;
  }
  int getScreenHeight() const {
    return (orientation == Portrait || orientation == PortraitInverted) ? EInkDisplay::DISPLAY_WIDTH
                                                                       : EInkDisplay::DISPLAY_HEIGHT;
  }
  void displayBuffer(EInkDisplay::RefreshMode = EInkDisplay::FAST_REFRESH, bool = false) const {}
  void displayWindow(int, int, int, int, bool = false) const {}
  void invertScreen() const {}
  void clearScreen(uint8_t = 0xFF) const {}
  void clearArea(int, int, int, int, uint8_t = 0xFF) const {}

  // Drawing - all no-ops
  void drawPixel(int, int, bool = true) const {}
  void drawLine(int, int, int, int, bool = true) const {}
  void drawRect(int, int, int, int, bool = true) const {}
  void fillRect(int, int, int, int, bool = true) const {}
  void drawImage(const uint8_t*, int, int, int, int) const {}
  void drawBitmap(const Bitmap&, int, int, int, int) const {}

  // Text measurement using real font metrics
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    if (!text || !*text) return 0;
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) return 0;
    const auto& font = it->second;
    int w = 0;
    const char* ptr = text;
    uint32_t cp;
    while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr)))) {
      const EpdGlyph* glyph = font.getGlyph(cp, style);
      if (!glyph) glyph = font.getGlyph('?', style);
      if (glyph) w += glyph->advanceX;
    }
    return w;
  }
  void drawCenteredText(int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  int getSpaceWidth(int fontId) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) return 5;
    const EpdGlyph* glyph = it->second.getGlyph(' ');
    return glyph ? glyph->advanceX : 5;
  }
  int getFontAscenderSize(int fontId) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) return 16;
    const EpdFontData* data = it->second.getData();
    return data ? data->ascender : 16;
  }
  int getLineHeight(int fontId) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) return 20;
    const EpdFontData* data = it->second.getData();
    return data ? data->advanceY : 20;
  }
  std::string truncatedText(int, const char* text, int, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    return text ? text : "";
  }

  std::vector<std::string> breakWordWithHyphenation(int fontId, const char* word, int maxWidth,
                                                     EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    std::vector<std::string> chunks;
    if (!word || *word == '\0') return chunks;

    std::string remaining = word;
    while (!remaining.empty()) {
      const int remainingWidth = getTextWidth(fontId, remaining.c_str(), style);
      if (remainingWidth <= maxWidth) {
        chunks.push_back(remaining);
        break;
      }

      // Find max chars that fit with hyphen
      std::string chunk;
      const char* ptr = remaining.c_str();
      const char* lastGoodPos = ptr;

      while (*ptr) {
        const char* nextChar = ptr;
        utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&nextChar));

        std::string testChunk = chunk;
        testChunk.append(ptr, nextChar - ptr);
        const int testWidth = getTextWidth(fontId, (testChunk + "-").c_str(), style);

        if (testWidth > maxWidth && !chunk.empty()) break;

        chunk = testChunk;
        lastGoodPos = nextChar;
        ptr = nextChar;
      }

      if (chunk.empty()) {
        // Single char too wide - force it
        const char* nextChar = remaining.c_str();
        utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&nextChar));
        chunk.append(remaining.c_str(), nextChar - remaining.c_str());
        lastGoodPos = nextChar;
      }

      if (lastGoodPos < remaining.c_str() + remaining.size()) {
        chunks.push_back(chunk + "-");
        remaining = remaining.substr(lastGoodPos - remaining.c_str());
      } else {
        chunks.push_back(chunk);
        remaining.clear();
      }
    }
    return chunks;
  }

  std::vector<std::string> wrapTextWithHyphenation(int, const char* text, int, int,
                                                    EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    std::vector<std::string> result;
    if (text) result.emplace_back(text);
    return result;
  }

  bool fontSupportsGrayscale(int) const { return false; }

  // Thai text - use same font metrics
  int getThaiTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    return getTextWidth(fontId, text, style);
  }
  void drawThaiText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}

  // Arabic text
  int getArabicTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    return getTextWidth(fontId, text, style);
  }
  void drawArabicText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}

  // UI Components
  void drawButtonHints(int, const char*, const char*, const char*, const char*, bool = true) const {}

  // Grayscale - no-ops
  void setRenderMode(const RenderMode mode) { this->renderMode = mode; }
  void copyGrayscaleLsbBuffers() const {}
  void copyGrayscaleMsbBuffers() const {}
  void displayGrayBuffer(bool = false) const {}
  bool storeBwBuffer() { return true; }
  void restoreBwBuffer() {}
  void cleanupGrayscaleWithFrameBuffer() const {}

  // Low level
  uint8_t* getFrameBuffer() const { return frameBuffer_; }
  static size_t getBufferSize() { return EInkDisplay::BUFFER_SIZE; }
  void grayscaleRevert() const {}
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
    *outTop = VIEWABLE_MARGIN_TOP;
    *outRight = VIEWABLE_MARGIN_RIGHT;
    *outBottom = VIEWABLE_MARGIN_BOTTOM;
    *outLeft = VIEWABLE_MARGIN_LEFT;
  }
};
