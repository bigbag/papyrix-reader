#pragma once

#include <FsHelpers.h>

#include <string>

class GfxRenderer;

namespace CoverHelpers {

struct CenteredRect {
  int x;
  int y;
  int width;
  int height;
};

// Calculate centered position maintaining aspect ratio
// Images smaller than viewport: centered without scaling
// Images larger than viewport: scaled down maintaining ratio
inline CenteredRect calculateCenteredRect(int imageWidth, int imageHeight, int viewportX, int viewportY,
                                          int viewportWidth, int viewportHeight) {
  CenteredRect result;
  result.width = viewportWidth;
  result.height = viewportHeight;

  if (imageWidth > viewportWidth || imageHeight > viewportHeight) {
    const float imgRatio = static_cast<float>(imageWidth) / static_cast<float>(imageHeight);
    const float vpRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);

    if (imgRatio > vpRatio) {
      result.x = viewportX;
      result.y = viewportY + (viewportHeight - static_cast<int>(viewportWidth / imgRatio)) / 2;
    } else {
      result.x = viewportX + (viewportWidth - static_cast<int>(viewportHeight * imgRatio)) / 2;
      result.y = viewportY;
    }
  } else {
    result.x = viewportX + (viewportWidth - imageWidth) / 2;
    result.y = viewportY + (viewportHeight - imageHeight) / 2;
  }

  return result;
}

// Render a cover BMP file with proper centering and grayscale support
// Returns true if cover was rendered successfully, false otherwise
// Updates pagesUntilFullRefresh based on refresh logic
// pagesPerRefreshValue: number of pages between full refreshes (from settings)
// turnOffScreen: power down display after refresh (sunlight fading fix)
bool renderCoverFromBmp(GfxRenderer& renderer, const std::string& bmpPath, int marginTop, int marginRight,
                        int marginBottom, int marginLeft, int& pagesUntilFullRefresh, int pagesPerRefreshValue,
                        bool turnOffScreen = false);

// Render cover with automatic fallback to preview if full cover not available
// previewPath: fast-generated preview (simple threshold, no dithering)
// coverPath: high-quality cover (with dithering)
// Prefers coverPath when available, falls back to previewPath
bool renderCoverWithFallback(GfxRenderer& renderer, const std::string& coverPath, const std::string& previewPath,
                             int marginTop, int marginRight, int marginBottom, int marginLeft,
                             int& pagesUntilFullRefresh, int pagesPerRefreshValue, bool turnOffScreen = false);

// Find a cover image file in the given directory
// Looks for: baseName.jpg, baseName.jpeg, baseName.png, baseName.bmp, cover.jpg, etc.
// Returns empty string if no cover found
std::string findCoverImage(const std::string& dirPath, const std::string& baseName);

// Convert an image file (JPG, PNG, or BMP) to BMP format
// For BMP input, just copies the file
// For JPG, uses JpegToBmpConverter (1-bit if use1BitDithering is true)
// For PNG, uses PngToBmpConverter
// logTag is used for Serial logging (e.g., "TXT", "MD ")
// Returns true on success
bool convertImageToBmp(const std::string& inputPath, const std::string& outputPath, const char* logTag,
                       bool use1BitDithering);

// Generate thumbnail BMP from full-size cover BMP
// Uses atomic write (temp file + rename) for safety
// Returns true on success
bool generateThumbFromCover(const std::string& coverBmpPath, const std::string& thumbBmpPath, const char* logTag);

}  // namespace CoverHelpers
