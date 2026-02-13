#pragma once
#include "EpdFont.h"

class EpdFontFamily {
 public:
  enum Style : uint8_t { REGULAR = 0, BOLD = 1, ITALIC = 2, BOLD_ITALIC = 3 };

  explicit EpdFontFamily(const EpdFont* regular, const EpdFont* bold = nullptr, const EpdFont* italic = nullptr,
                         const EpdFont* boldItalic = nullptr)
      : regular(regular), bold(bold), italic(italic), boldItalic(boldItalic) {}
  ~EpdFontFamily() = default;
  void getTextDimensions(const char* string, int* w, int* h, Style style = REGULAR) const;
  bool hasPrintableChars(const char* string, Style style = REGULAR) const;
  const EpdFontData* getData(Style style = REGULAR) const;
  const EpdGlyph* getGlyph(uint32_t cp, Style style = REGULAR) const;

  void setFont(Style style, const EpdFont* font) {
    switch (style) {
      case BOLD:
        bold = font;
        break;
      case ITALIC:
        italic = font;
        break;
      case BOLD_ITALIC:
        boldItalic = font;
        break;
      default:
        break;
    }
  }

  const EpdFont* getFont(Style style) const;

 private:
  const EpdFont* regular;
  const EpdFont* bold;
  const EpdFont* italic;
  const EpdFont* boldItalic;
};

// Backward-compatible aliases for code using the old global enum values
constexpr auto REGULAR = EpdFontFamily::REGULAR;
constexpr auto BOLD = EpdFontFamily::BOLD;
constexpr auto ITALIC = EpdFontFamily::ITALIC;
constexpr auto BOLD_ITALIC = EpdFontFamily::BOLD_ITALIC;
