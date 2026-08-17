#pragma once

#include <functional>
#include <string>

class GfxRenderer;

namespace CoverHelpers {

inline bool isAbortRequested(const std::function<bool()>& shouldAbort) {
  return shouldAbort && shouldAbort();
}

struct CenteredRect {
  int x, y, width, height;
};

inline CenteredRect calculateCenteredRect(int, int, int, int, int, int) { return {0, 0, 0, 0}; }

inline bool renderCoverFromBmp(GfxRenderer&, const std::string&, int, int, int, int, int&, int, bool = false) {
  return false;
}

inline std::string findCoverImage(const std::string&, const std::string&,
                                  const std::function<bool()>& = nullptr) {
  return "";
}

inline bool convertImageToBmp(const std::string&, const std::string&, const char*, bool,
                              const std::function<bool()>& = nullptr) {
  return false;
}

}  // namespace CoverHelpers
