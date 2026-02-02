#include "CoverHelpers.h"

#include <Bitmap.h>
#include <BitmapHelpers.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <ImageConverter.h>
#include <SDCardManager.h>

#include "../../src/config.h"

namespace CoverHelpers {

bool renderCoverWithFallback(GfxRenderer& renderer, const std::string& coverPath, const std::string& previewPath,
                             int marginTop, int marginRight, int marginBottom, int marginLeft,
                             int& pagesUntilFullRefresh, int pagesPerRefreshValue, bool turnOffScreen) {
  // Prefer full cover if available
  if (SdMan.exists(coverPath.c_str())) {
    return renderCoverFromBmp(renderer, coverPath, marginTop, marginRight, marginBottom, marginLeft,
                              pagesUntilFullRefresh, pagesPerRefreshValue, turnOffScreen);
  }
  // Fall back to preview
  if (SdMan.exists(previewPath.c_str())) {
    Serial.printf("[%lu] [CVR] Using preview cover (full cover not ready)\n", millis());
    return renderCoverFromBmp(renderer, previewPath, marginTop, marginRight, marginBottom, marginLeft,
                              pagesUntilFullRefresh, pagesPerRefreshValue, turnOffScreen);
  }
  return false;
}

bool renderCoverFromBmp(GfxRenderer& renderer, const std::string& bmpPath, int marginTop, int marginRight,
                        int marginBottom, int marginLeft, int& pagesUntilFullRefresh, int pagesPerRefreshValue,
                        bool turnOffScreen) {
  FsFile coverFile;
  if (!SdMan.openFileForRead("CVR", bmpPath, coverFile)) {
    Serial.printf("[%lu] [CVR] Failed to open cover BMP: %s\n", millis(), bmpPath.c_str());
    return false;
  }

  Bitmap bitmap(coverFile);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    coverFile.close();
    Serial.printf("[%lu] [CVR] Failed to parse cover BMP headers\n", millis());
    return false;
  }

  // Calculate viewport (accounting for margins)
  const int viewportWidth = renderer.getScreenWidth() - marginLeft - marginRight;
  const int viewportHeight = renderer.getScreenHeight() - marginTop - marginBottom;

  // Center image in viewport
  auto rect = calculateCenteredRect(bitmap.getWidth(), bitmap.getHeight(), marginLeft, marginTop, viewportWidth,
                                    viewportHeight);

  renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);

  // Display with refresh logic
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(EInkDisplay::HALF_REFRESH, turnOffScreen);
    pagesUntilFullRefresh = pagesPerRefreshValue;
  } else {
    renderer.displayBuffer(EInkDisplay::FAST_REFRESH, turnOffScreen);
    pagesUntilFullRefresh--;
  }

  // Grayscale rendering (if bitmap supports it and buffer can be stored)
  if (bitmap.hasGreyscale() && renderer.storeBwBuffer()) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer(turnOffScreen);
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
  }

  coverFile.close();
  Serial.printf("[%lu] [CVR] Rendered cover from BMP\n", millis());
  return true;
}

std::string findCoverImage(const std::string& dirPath, const std::string& baseName) {
  // Extensions to search for, in priority order
  const char* extensions[] = {".jpg", ".jpeg", ".png", ".bmp"};

  // Priority 1: Image matching base filename (e.g., book.jpg for book.txt)
  for (const char* ext : extensions) {
    std::string imagePath = dirPath + "/" + baseName + ext;
    if (SdMan.exists(imagePath.c_str())) {
      Serial.printf("[%lu] [CVR] Found cover image: %s\n", millis(), imagePath.c_str());
      return imagePath;
    }
  }

  // Priority 2: Generic cover image in same directory
  for (const char* ext : extensions) {
    std::string imagePath = dirPath + "/cover" + ext;
    if (SdMan.exists(imagePath.c_str())) {
      Serial.printf("[%lu] [CVR] Found cover image: %s\n", millis(), imagePath.c_str());
      return imagePath;
    }
  }

  return "";
}

bool convertImageToBmp(const std::string& inputPath, const std::string& outputPath, const char* logTag,
                       bool use1BitDithering) {
  ImageConvertConfig config;
  config.oneBit = use1BitDithering;
  config.logTag = logTag;
  return ImageConverterFactory::convertToBmp(inputPath, outputPath, config);
}

bool generateThumbFromCover(const std::string& coverBmpPath, const std::string& thumbBmpPath, const char* logTag) {
  if (SdMan.exists(thumbBmpPath.c_str())) return true;
  if (!SdMan.exists(coverBmpPath.c_str())) return false;

  const auto thumbTempPath = thumbBmpPath + ".tmp";
  if (bmpTo1BitBmpScaled(coverBmpPath.c_str(), thumbTempPath.c_str(), THUMB_WIDTH, THUMB_HEIGHT)) {
    FsFile tempFile = SdMan.open(thumbTempPath.c_str(), O_RDWR);
    if (tempFile) {
      tempFile.rename(thumbBmpPath.c_str());
      tempFile.close();
      Serial.printf("[%lu] [%s] Generated thumbnail\n", millis(), logTag);
      return true;
    }
  }
  SdMan.remove(thumbTempPath.c_str());
  return false;
}

}  // namespace CoverHelpers
