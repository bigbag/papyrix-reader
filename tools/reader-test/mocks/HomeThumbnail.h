#pragma once

#include <cstdint>
#include <functional>
#include <string>

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

inline std::string pathForCache(const std::string& cachePath) { return cachePath + "/" + FILE_NAME; }
inline std::string coverPathForCache(const std::string& cachePath) { return cachePath + "/" + COVER_FILE_NAME; }
inline HomeImageSelection selectForHome(bool, const std::string&, const std::string&) { return {}; }
inline void fitDimensions(int sourceWidth, int sourceHeight, int& destinationWidth, int& destinationHeight) {
  destinationWidth = sourceWidth;
  destinationHeight = sourceHeight;
}
inline bool validate(const std::string&, Info* = nullptr) { return false; }
inline bool validateCover(const std::string&, Info* = nullptr) { return false; }
inline Result generateFromCover(const std::string&, const std::string&, const std::function<bool()>& = nullptr) {
  return Result::Unavailable;
}

}  // namespace home_thumbnail
