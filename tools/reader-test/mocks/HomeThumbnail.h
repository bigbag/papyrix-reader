#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Configurable HomeThumbnail mock for reader-test. Defaults reproduce the old
// behavior (no images, all validation/generation fails); tests and scenarios
// flip the switches to exercise thumbnail-first, cover-fallback, and
// disabled-images behavior of the real selection pipeline.

namespace home_thumbnail {

constexpr int MAX_WIDTH = 320;
constexpr int MAX_HEIGHT = 440;
constexpr const char* FILE_NAME = "thumb.bmp";
constexpr const char* COVER_FILE_NAME = "cover.bmp";

enum class HomeImageType : uint8_t {
  None,
  Thumbnail,
  Cover,
};

struct HomeImageSelection {
  HomeImageType type = HomeImageType::None;
  std::string path;
};

enum class Result : uint8_t {
  Ready,
  Unavailable,
  Cancelled,
};

struct Info {
  int width = 0;
  int height = 0;
};

struct MockState {
  bool thumbnailValid = false;      // validate(thumbnailPath)
  bool coverValid = false;          // validateCover(coverPath)
  Result generateResult = Result::Unavailable;
  bool generateWasCalled = false;
  bool validateWasCalled = false;
};
inline MockState g_mockState;

inline std::string pathForCache(const std::string& cachePath) { return cachePath + "/" + FILE_NAME; }
inline std::string coverPathForCache(const std::string& cachePath) { return cachePath + "/" + COVER_FILE_NAME; }

inline HomeImageSelection selectForHome(const bool imagesEnabled, const std::string& thumbnailPath,
                                        const std::string& coverPath) {
  if (!imagesEnabled) return {};
  if (g_mockState.thumbnailValid) return {HomeImageType::Thumbnail, thumbnailPath};
  if (g_mockState.coverValid) return {HomeImageType::Cover, coverPath};
  return {};
}

inline void fitDimensions(int sourceWidth, int sourceHeight, int& destinationWidth, int& destinationHeight) {
  destinationWidth = sourceWidth;
  destinationHeight = sourceHeight;
}

inline bool validate(const std::string&, Info* = nullptr) {
  g_mockState.validateWasCalled = true;
  return g_mockState.thumbnailValid;
}
inline bool validateCover(const std::string&, Info* = nullptr) { return g_mockState.coverValid; }

inline Result generateFromCover(const std::string&, const std::string&, const std::function<bool()>& = nullptr) {
  g_mockState.generateWasCalled = true;
  return g_mockState.generateResult;
}

inline void resetMockState() { g_mockState = MockState{}; }

}  // namespace home_thumbnail
