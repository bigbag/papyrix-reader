#pragma once

#include <Bitmap.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;

class BitmapTurnCache {
 public:
  explicit BitmapTurnCache(GfxRenderer& renderer);
  ~BitmapTurnCache();
  BitmapTurnCache(const BitmapTurnCache&) = delete;
  BitmapTurnCache& operator=(const BitmapTurnCache&) = delete;

  Bitmap* get(const std::string& path);

 private:
  struct Entry;
  GfxRenderer& renderer_;
  std::vector<std::unique_ptr<Entry>> entries_;
  size_t bytes_ = 0;
};
