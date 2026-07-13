#pragma once

#include <algorithm>
#include <cstdint>

#include "TextBlock.h"

struct BlockStyle {
  static constexpr float MAX_HORIZONTAL_INSET_EM = 2.0f;

  TextBlock::BLOCK_STYLE alignment = TextBlock::JUSTIFIED;

  int16_t marginTop = 0;
  int16_t marginBottom = 0;
  int16_t marginLeft = 0;
  int16_t marginRight = 0;
  int16_t paddingTop = 0;
  int16_t paddingBottom = 0;
  int16_t paddingLeft = 0;
  int16_t paddingRight = 0;
  bool textAlignDefined = false;
  bool isRtl = false;
  bool directionDefined = false;

  int16_t leftInset() const { return static_cast<int16_t>(marginLeft + paddingLeft); }
  int16_t rightInset() const { return static_cast<int16_t>(marginRight + paddingRight); }
  int16_t totalHorizontalInset() const { return static_cast<int16_t>(leftInset() + rightInset()); }
  int16_t topInset() const { return static_cast<int16_t>(marginTop + paddingTop); }
  int16_t bottomInset() const { return static_cast<int16_t>(marginBottom + paddingBottom); }

  BlockStyle withoutBottom() const {
    BlockStyle result = *this;
    result.marginBottom = 0;
    result.paddingBottom = 0;
    return result;
  }

  BlockStyle addBottom(const BlockStyle& source) const {
    BlockStyle result = *this;
    result.marginBottom = std::max(marginBottom, source.marginBottom);
    result.paddingBottom = static_cast<int16_t>(paddingBottom + source.paddingBottom);
    return result;
  }

  enum class CombineAxis : uint8_t {
    Horizontal = 1,
    Vertical = 2,
  };

  BlockStyle getCombinedBlockStyle(const BlockStyle& child, CombineAxis axis) const {
    BlockStyle result = child;

    if (axis == CombineAxis::Horizontal) {
      result.marginLeft = static_cast<int16_t>(child.marginLeft + marginLeft);
      result.marginRight = static_cast<int16_t>(child.marginRight + marginRight);
      result.paddingLeft = static_cast<int16_t>(child.paddingLeft + paddingLeft);
      result.paddingRight = static_cast<int16_t>(child.paddingRight + paddingRight);
      if (!child.textAlignDefined && textAlignDefined) {
        result.alignment = alignment;
        result.textAlignDefined = true;
      }
    } else {
      result.marginTop = std::max(child.marginTop, marginTop);
      result.marginBottom = std::max(child.marginBottom, marginBottom);
      result.paddingTop = static_cast<int16_t>(child.paddingTop + paddingTop);
      result.paddingBottom = static_cast<int16_t>(child.paddingBottom + paddingBottom);
    }

    if (!child.directionDefined && directionDefined) {
      result.isRtl = isRtl;
      result.directionDefined = true;
    }

    return result;
  }
};
