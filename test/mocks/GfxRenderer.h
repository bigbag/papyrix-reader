#pragma once

#include <Display.h>
#include <EpdFontFamily.h>
#include <Utf8.h>

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Bitmap;
class ExternalFont;
class StreamingEpdFont;

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

  struct CenteredTextCall {
    int fontId;
    int y;
    std::string text;
    bool black;
    EpdFontFamily::Style style;
  };

  struct FillRectCall {
    int x;
    int y;
    int w;
    int h;
    bool color;
  };

 private:
  papyrix::hal::Display& einkDisplay;
  RenderMode renderMode;
  Orientation orientation;
  std::map<int, EpdFontFamily> fontMap;
  static uint8_t frameBuffer_[papyrix::hal::Display::BUFFER_SIZE];
  mutable int lastWrapMaxWidth_ = 0;
  mutable int lastWrapMaxLines_ = 0;
  mutable std::vector<CenteredTextCall> centeredTextCalls_;
  std::vector<std::string> wrappedTextResult_;
  mutable std::string lastText_;
  mutable std::vector<FillRectCall> fillRects_;

 public:
  static constexpr int BUTTON_HINT_WIDTH = 106;
  static constexpr int BUTTON_HINT_MAX_TEXT_WIDTH = 94;
  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  explicit GfxRenderer(papyrix::hal::Display& einkDisplay)
      : einkDisplay(einkDisplay), renderMode(BW), orientation(Portrait) {}

  void begin() {}
  void insertFont(int fontId, EpdFontFamily font) { fontMap.emplace(fontId, font); }
  void clearWidthCache() {}
  void setExternalFont(ExternalFont*) {}
  ExternalFont* getExternalFont() const { return nullptr; }

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

  int getSpaceWidth(int fontId) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) return 5;
    const EpdGlyph* glyph = it->second.getGlyph(' ');
    return glyph ? glyph->advanceX : 5;
  }

  int getLineHeight(int fontId) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) return 20;
    const EpdFontData* data = it->second.getData();
    return data ? data->advanceY : 20;
  }

  int getEffectiveLineHeight(int fontId) const { return getLineHeight(fontId); }
  int getScreenWidth() const { return orientation == Portrait || orientation == PortraitInverted ? 480 : 800; }
  int getScreenHeight() const { return orientation == Portrait || orientation == PortraitInverted ? 800 : 480; }

  int getFontAscenderSize(int fontId) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) return 16;
    const EpdFontData* data = it->second.getData();
    return data ? data->ascender : 16;
  }

  bool hasGlyph(int fontId, uint32_t cp) const {
    auto it = fontMap.find(fontId);
    return it != fontMap.end() && it->second.getGlyph(cp, EpdFontFamily::REGULAR) != nullptr;
  }

  int getThaiTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    return getTextWidth(fontId, text, style);
  }
  int getArabicTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    return getTextWidth(fontId, text, style);
  }

  void setWrappedTextResult(std::vector<std::string> lines) { wrappedTextResult_ = std::move(lines); }
  int lastWrapMaxWidth() const { return lastWrapMaxWidth_; }
  int lastWrapMaxLines() const { return lastWrapMaxLines_; }
  const std::vector<CenteredTextCall>& centeredTextCalls() const { return centeredTextCalls_; }
  void clearCenteredTextCalls() const { centeredTextCalls_.clear(); }
  const std::string& lastText() const { return lastText_; }
  std::string truncatedText(int, const char* text, int) const { return text ? text : ""; }

  std::vector<std::string> wrapTextWithHyphenation(int, const char* text, int maxWidth, int maxLines,
                                                   EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    lastWrapMaxWidth_ = maxWidth;
    lastWrapMaxLines_ = maxLines;
    if (!wrappedTextResult_.empty()) return wrappedTextResult_;
    return text && *text ? std::vector<std::string>{text} : std::vector<std::string>{};
  }

  std::vector<std::string> breakWordWithHyphenation(int fontId, const char* word, int maxWidth,
                                                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    std::vector<std::string> chunks;
    if (!word || *word == '\0') return chunks;
    std::string remaining = word;
    while (!remaining.empty()) {
      if (getTextWidth(fontId, remaining.c_str(), style) <= maxWidth) {
        chunks.push_back(remaining);
        break;
      }
      std::string chunk;
      const char* ptr = remaining.c_str();
      const char* lastGood = ptr;
      while (*ptr) {
        const char* next = ptr;
        utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&next));
        std::string test = chunk;
        test.append(ptr, next - ptr);
        if (getTextWidth(fontId, (test + "-").c_str(), style) > maxWidth && !chunk.empty()) break;
        chunk = test;
        lastGood = next;
        ptr = next;
      }
      if (chunk.empty()) {
        const char* next = remaining.c_str();
        utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&next));
        chunk.append(remaining.c_str(), next - remaining.c_str());
        lastGood = next;
      }
      if (lastGood < remaining.c_str() + remaining.size()) {
        chunks.push_back(chunk + "-");
        remaining = remaining.substr(lastGood - remaining.c_str());
      } else {
        chunks.push_back(chunk);
        remaining.clear();
      }
    }
    return chunks;
  }

  void drawText(int, int, int, const char* text, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    lastText_ = text ? text : "";
  }
  void drawCenteredText(int fontId, int y, const char* text, bool black = true,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    centeredTextCalls_.push_back({fontId, y, text ? text : "", black, style});
  }
  void drawThaiText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawArabicText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void clearArea(int, int, int, int, uint8_t = 0xFF) const {}
  void warmCodepointsBatch(int, const uint32_t*, size_t, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawImage(const uint8_t* data, int x, int y, int width, int height) const {
    if (data == nullptr || width <= 0 || height <= 0 || x < 0 || x >= getScreenWidth() || y < 0 ||
        y >= getScreenHeight()) {
      return;
    }

    int panelX = x;
    int panelY = y;
    switch (orientation) {
      case Portrait:
        panelX = y;
        panelY = papyrix::hal::Display::DISPLAY_HEIGHT - 1 - x;
        break;
      case LandscapeClockwise:
        panelX = papyrix::hal::Display::DISPLAY_WIDTH - 1 - x;
        panelY = papyrix::hal::Display::DISPLAY_HEIGHT - 1 - y;
        break;
      case PortraitInverted:
        panelX = papyrix::hal::Display::DISPLAY_WIDTH - 1 - y;
        panelY = x;
        break;
      case LandscapeCounterClockwise:
        break;
    }

    const int imageWidthBytes = width / 8;
    const int firstByte = panelX / 8;
    const int availableBytes = papyrix::hal::Display::DISPLAY_WIDTH_BYTES - firstByte;
    const int copyBytes = imageWidthBytes < availableBytes ? imageWidthBytes : availableBytes;
    const int availableRows = papyrix::hal::Display::DISPLAY_HEIGHT - panelY;
    const int copyRows = height < availableRows ? height : availableRows;
    if (copyBytes <= 0 || copyRows <= 0) return;

    for (int row = 0; row < copyRows; row++) {
      memcpy(&frameBuffer_[(panelY + row) * papyrix::hal::Display::DISPLAY_WIDTH_BYTES + firstByte],
             &data[row * imageWidthBytes], copyBytes);
    }
  }
  void clearScreen(uint8_t color = 0xFF) const { memset(frameBuffer_, color, papyrix::hal::Display::BUFFER_SIZE); }
  void invertScreen() const {
    for (uint8_t& byte : frameBuffer_) byte = static_cast<uint8_t>(~byte);
  }
  void drawPixel(int, int, bool = true) const {}
  void drawLine(int, int, int, int, bool = true) const {}
  void drawRect(int, int, int, int, bool = true) const {}
  void drawBitmap(const Bitmap&, int, int, int, int) const {}
  void fillRect(int x, int y, int w, int h, bool color = true) const { fillRects_.push_back({x, y, w, h, color}); }
  void clearFillRects() const { fillRects_.clear(); }
  const std::vector<FillRectCall>& fillRects() const { return fillRects_; }
  void displayBuffer(papyrix::hal::Display::RefreshMode = papyrix::hal::Display::FAST_REFRESH, bool = false) const {}
  bool supportsGrayscale() const { return true; }
  void copyGrayscaleLsbBuffers() const {}
  void copyGrayscaleMsbBuffers() const {}
  void displayGrayBuffer(bool = false) const {}
  void cleanupGrayscaleWithFrameBuffer() const {}

  uint8_t* getFrameBuffer() const { return frameBuffer_; }
  static size_t getBufferSize() { return papyrix::hal::Display::BUFFER_SIZE; }
};
