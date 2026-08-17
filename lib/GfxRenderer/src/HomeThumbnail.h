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

std::string pathForCache(const std::string& cachePath);
std::string coverPathForCache(const std::string& cachePath);
HomeImageSelection selectForHome(bool imagesEnabled, const std::string& thumbnailPath, const std::string& coverPath);
void fitDimensions(int sourceWidth, int sourceHeight, int& destinationWidth, int& destinationHeight);
bool validate(const std::string& path, Info* info = nullptr);
bool validateCover(const std::string& path, Info* info = nullptr);
Result generateFromCover(const std::string& coverPath, const std::string& thumbnailPath,
                         const std::function<bool()>& shouldAbort = nullptr);

}  // namespace home_thumbnail
