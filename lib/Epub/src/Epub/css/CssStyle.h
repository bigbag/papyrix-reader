#pragma once

#include <cmath>
#include <cstdint>

enum class CssUnit : uint8_t { Pixels = 0, Em = 1, Rem = 2, Points = 3, Percent = 4 };

struct CssLength {
  float value = 0.0f;
  CssUnit unit = CssUnit::Pixels;

  CssLength() = default;
  CssLength(const float v, const CssUnit u) : value(v), unit(u) {}
  explicit CssLength(const float pixels) : value(pixels) {}

  float toPixels(const float emSize, const float containerWidth = 0) const {
    switch (unit) {
      case CssUnit::Em:
      case CssUnit::Rem:
        return value * emSize;
      case CssUnit::Points:
        return value * 1.33f;
      case CssUnit::Percent:
        return value * containerWidth / 100.0f;
      default:
        return value;
    }
  }

  int16_t toPixelsInt16(const float emSize, const float containerWidth = 0) const {
    return static_cast<int16_t>(std::lround(toPixels(emSize, containerWidth)));
  }
};

enum class TextAlign : uint8_t { None, Left, Right, Center, Justify };

enum class CssFontStyle : uint8_t { Normal, Italic };

enum class CssFontWeight : uint8_t { Normal, Bold };

enum class TextDirection : uint8_t { Ltr, Rtl };

enum class CssDisplay : uint8_t { Block, None };

struct CssPropertyFlags {
  uint32_t textAlign : 1;
  uint32_t fontStyle : 1;
  uint32_t fontWeight : 1;
  uint32_t direction : 1;
  uint32_t marginTop : 1;
  uint32_t marginBottom : 1;
  uint32_t marginLeft : 1;
  uint32_t marginRight : 1;
  uint32_t paddingTop : 1;
  uint32_t paddingBottom : 1;
  uint32_t paddingLeft : 1;
  uint32_t paddingRight : 1;
  uint32_t display : 1;

  CssPropertyFlags()
      : textAlign(0),
        fontStyle(0),
        fontWeight(0),
        direction(0),
        marginTop(0),
        marginBottom(0),
        marginLeft(0),
        marginRight(0),
        paddingTop(0),
        paddingBottom(0),
        paddingLeft(0),
        paddingRight(0),
        display(0) {}

  bool anySet() const {
    return textAlign || fontStyle || fontWeight || direction || marginTop || marginBottom || marginLeft ||
           marginRight || paddingTop || paddingBottom || paddingLeft || paddingRight || display;
  }

  void clearAll() {
    textAlign = fontStyle = fontWeight = direction = 0;
    marginTop = marginBottom = marginLeft = marginRight = 0;
    paddingTop = paddingBottom = paddingLeft = paddingRight = 0;
    display = 0;
  }
};

struct CssStyle {
  TextAlign textAlign = TextAlign::None;
  CssFontStyle fontStyle = CssFontStyle::Normal;
  CssFontWeight fontWeight = CssFontWeight::Normal;
  TextDirection direction = TextDirection::Ltr;
  CssDisplay display = CssDisplay::Block;

  CssLength marginTop;
  CssLength marginBottom;
  CssLength marginLeft;
  CssLength marginRight;
  CssLength paddingTop;
  CssLength paddingBottom;
  CssLength paddingLeft;
  CssLength paddingRight;

  CssPropertyFlags defined;

  bool hasTextAlign() const { return defined.textAlign; }
  bool hasFontStyle() const { return defined.fontStyle; }
  bool hasFontWeight() const { return defined.fontWeight; }
  bool hasDirection() const { return defined.direction; }
  bool hasMarginTop() const { return defined.marginTop; }
  bool hasMarginBottom() const { return defined.marginBottom; }
  bool hasMarginLeft() const { return defined.marginLeft; }
  bool hasMarginRight() const { return defined.marginRight; }
  bool hasPaddingTop() const { return defined.paddingTop; }
  bool hasPaddingBottom() const { return defined.paddingBottom; }
  bool hasPaddingLeft() const { return defined.paddingLeft; }
  bool hasPaddingRight() const { return defined.paddingRight; }
  bool hasDisplay() const { return defined.display; }

  void applyOver(const CssStyle& other) {
    if (other.hasTextAlign()) {
      textAlign = other.textAlign;
      defined.textAlign = 1;
    }
    if (other.hasFontStyle()) {
      fontStyle = other.fontStyle;
      defined.fontStyle = 1;
    }
    if (other.hasFontWeight()) {
      fontWeight = other.fontWeight;
      defined.fontWeight = 1;
    }
    if (other.hasDirection()) {
      direction = other.direction;
      defined.direction = 1;
    }
    if (other.hasMarginTop()) {
      marginTop = other.marginTop;
      defined.marginTop = 1;
    }
    if (other.hasMarginBottom()) {
      marginBottom = other.marginBottom;
      defined.marginBottom = 1;
    }
    if (other.hasMarginLeft()) {
      marginLeft = other.marginLeft;
      defined.marginLeft = 1;
    }
    if (other.hasMarginRight()) {
      marginRight = other.marginRight;
      defined.marginRight = 1;
    }
    if (other.hasPaddingTop()) {
      paddingTop = other.paddingTop;
      defined.paddingTop = 1;
    }
    if (other.hasPaddingBottom()) {
      paddingBottom = other.paddingBottom;
      defined.paddingBottom = 1;
    }
    if (other.hasPaddingLeft()) {
      paddingLeft = other.paddingLeft;
      defined.paddingLeft = 1;
    }
    if (other.hasPaddingRight()) {
      paddingRight = other.paddingRight;
      defined.paddingRight = 1;
    }
    if (other.hasDisplay()) {
      display = other.display;
      defined.display = 1;
    }
  }

  void reset() {
    textAlign = TextAlign::None;
    fontStyle = CssFontStyle::Normal;
    fontWeight = CssFontWeight::Normal;
    direction = TextDirection::Ltr;
    display = CssDisplay::Block;
    marginTop = marginBottom = marginLeft = marginRight = CssLength{};
    paddingTop = paddingBottom = paddingLeft = paddingRight = CssLength{};
    defined.clearAll();
  }
};
