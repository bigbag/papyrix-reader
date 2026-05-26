#pragma once

#include <EInkDisplay.h>
#include <EpdFontFamily.h>
#include <Utf8.h>

#include <map>
#include <string>
#include <vector>

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
  static uint8_t frameBuffer_[EInkDisplay::BUFFER_SIZE];

 public:
  static constexpr int VIEWABLE_MARGIN_TOP = 9;
  static constexpr int VIEWABLE_MARGIN_RIGHT = 3;
  static constexpr int VIEWABLE_MARGIN_BOTTOM = 3;
  static constexpr int VIEWABLE_MARGIN_LEFT = 3;

  explicit GfxRenderer(EInkDisplay& einkDisplay) : einkDisplay(einkDisplay), renderMode(BW), orientation(Portrait) {}

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

  void drawText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawCenteredText(int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawThaiText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawArabicText(int, int, int, const char*, bool = true, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void clearArea(int, int, int, int, uint8_t = 0xFF) const {}
  void warmCodepointsBatch(int, const uint32_t*, size_t, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {}
  void drawImage(const uint8_t*, int, int, int, int) const {}

  uint8_t* getFrameBuffer() const { return frameBuffer_; }
  static size_t getBufferSize() { return EInkDisplay::BUFFER_SIZE; }
};
