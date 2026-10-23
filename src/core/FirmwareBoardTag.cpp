#include "FirmwareBoardTag.h"

#include <TargetConfig.h>

#include <array>

// Adapted from CrossMux eaf5c73a71aaa03e372b6bc274923c2cc621a033. See CROSSMUX_LICENSE.
namespace papyrix::firmware_board {

#if PAPYRIX_TARGET_XTEINK_C3
[[gnu::used]] constexpr char kTag[] = "PAPYRIX-BOARD-V1:xteink_c3;";
#elif PAPYRIX_TARGET_X4PRO
[[gnu::used]] constexpr char kTag[] = "PAPYRIX-BOARD-V1:x4pro;";
#elif PAPYRIX_TARGET_X4CLASSIC
[[gnu::used]] constexpr char kTag[] = "PAPYRIX-BOARD-V1:x4c;";
#elif PAPYRIX_TARGET_STICKY
[[gnu::used]] constexpr char kTag[] = "PAPYRIX-BOARD-V1:sticky;";
#endif

namespace {
constexpr size_t kMagicLength = sizeof("PAPYRIX-BOARD-V1:") - 1;
constexpr size_t kNameLength = sizeof(kTag) - kMagicLength - 2;
constexpr auto kPrefix = [] {
  std::array<uint8_t, kMagicLength> prefix{};
  for (size_t i = 1; i < kMagicLength; ++i) {
    size_t matched = prefix[i - 1];
    while (matched && kTag[i] != kTag[matched]) matched = prefix[matched - 1];
    if (kTag[i] == kTag[matched]) ++matched;
    prefix[i] = static_cast<uint8_t>(matched);
  }
  return prefix;
}();
}  // namespace

void Scanner::feed(const uint8_t* data, size_t length) {
  if (invalid_) return;
  for (size_t i = 0; i < length; ++i) {
    const char value = static_cast<char>(data[i]);
    if (capturing_) {
      if (value == ';') {
        invalid_ = found_ || nameLength_ != kNameLength;
        found_ = true;
        capturing_ = false;
        if (invalid_) return;
      } else if (nameLength_ >= kNameLength || value != kTag[kMagicLength + nameLength_]) {
        invalid_ = true;
        return;
      } else {
        ++nameLength_;
      }
      continue;
    }
    while (matched_ && value != kTag[matched_]) matched_ = kPrefix[matched_ - 1];
    if (value == kTag[matched_]) ++matched_;
    if (matched_ == kMagicLength) {
      matched_ = 0;
      nameLength_ = 0;
      capturing_ = true;
    }
  }
}

void Scanner::endSegment() {
  if (capturing_) invalid_ = true;
  capturing_ = false;
  matched_ = 0;
}

}  // namespace papyrix::firmware_board
