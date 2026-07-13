#include "ImageConverter.h"

#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>

#define TAG "IMG_CONV"
#include <PngToBmpConverter.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

class JpegImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    // Quick mode: simple threshold instead of dithering
    if (config.quickMode) {
      return JpegToBmpConverter::jpegFileToBmpStreamQuick(input, output, config.maxWidth, config.maxHeight);
    }
    if (config.maxWidth == 450 && config.maxHeight == 750 && !config.shouldAbort) {
      return config.oneBit ? JpegToBmpConverter::jpegFileTo1BitBmpStream(input, output)
                           : JpegToBmpConverter::jpegFileToBmpStream(input, output);
    }
    return config.oneBit
               ? JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight)
               : JpegToBmpConverter::jpegFileToBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight,
                                                                 config.shouldAbort);
  }

  const char* formatName() const override { return "JPEG"; }
};

class PngImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    // Quick mode: simple threshold instead of dithering
    if (config.quickMode) {
      return PngToBmpConverter::pngFileToBmpStreamQuick(input, output, config.maxWidth, config.maxHeight);
    }
    // Note: PNG converter always produces 2-bit output. Unlike JPEG, PNG does not support
    // 1-bit dithering (oneBit flag is ignored). PNG thumbnails will be slightly larger but
    // render at the same speed since the display hardware handles both formats equally.
    return PngToBmpConverter::pngFileToBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight,
                                                         config.shouldAbort);
  }

  const char* formatName() const override { return "PNG"; }
};

class BmpImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    (void)config;
    uint8_t buffer[512];
    while (input.available()) {
      size_t bytesRead = input.read(buffer, sizeof(buffer));
      if (output.write(buffer, bytesRead) != bytesRead) {
        return false;
      }
    }
    return true;
  }

  const char* formatName() const override { return "BMP"; }
};

JpegImageConverter jpegConverter;
PngImageConverter pngConverter;
BmpImageConverter bmpConverter;

}  // namespace

ImageConverter* ImageConverterFactory::getConverter(const std::string& filePath) {
  if (FsHelpers::isJpegFile(filePath)) {
    return &jpegConverter;
  }
  if (FsHelpers::isPngFile(filePath)) {
    return &pngConverter;
  }
  if (FsHelpers::isBmpFile(filePath)) {
    return &bmpConverter;
  }
  return nullptr;
}

bool ImageConverterFactory::convertToBmp(const std::string& inputPath, const std::string& outputPath,
                                         const ImageConvertConfig& config) {
  ImageConverter* converter = getConverter(inputPath);
  if (!converter) {
    LOG_ERR(config.logTag, "Unsupported image format: %s", inputPath.c_str());
    return false;
  }

  // Stack safety gate: PNG/JPEG decode (pngle + zlib/tinflate) is the deepest call chain in
  // the reader and can overflow a constrained task stack, panicking the whole device. If the
  // current task's free stack is below the safety floor, skip this image gracefully instead —
  // the caller writes a .failed marker and session-blacklists the hash, so the page renders
  // without the image rather than rebooting. The 12 KB loopTask (see ARDUINO_LOOP_STACK_SIZE)
  // keeps this gate from triggering in normal use; it only fires when the stack is genuinely
  // tight (deeper-than-expected nesting, huge image), which is exactly when a skip beats a crash.
  constexpr size_t kMinImageStackBytes = 4096;
  if (uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t) < kMinImageStackBytes) {
    LOG_WRN(config.logTag, "Skip image convert (low stack): %s", inputPath.c_str());
    return false;
  }

  FsFile inputFile;
  if (!SdMan.openFileForRead(config.logTag, inputPath, inputFile)) {
    LOG_ERR(config.logTag, "Failed to open input file: %s", inputPath.c_str());
    return false;
  }

  // Atomic publish: convert to .part then rename so readers never see a half-written BMP.
  const std::string partPath = outputPath + ".part";
  SdMan.remove(partPath.c_str());

  FsFile outputFile;
  if (!SdMan.openFileForWrite(config.logTag, partPath, outputFile)) {
    inputFile.close();
    LOG_ERR(config.logTag, "Failed to create output file: %s", partPath.c_str());
    return false;
  }

  const bool success = converter->convert(inputFile, outputFile, config);

  inputFile.close();
  outputFile.close();

  if (!success) {
    LOG_ERR(config.logTag, "Failed to convert %s to BMP", converter->formatName());
    SdMan.remove(partPath.c_str());
    return false;
  }

  // Publish the completed BMP (commitFile removes a stale output first because
  // SdFat can't rename over an existing file).
  if (!SdMan.commitFile(partPath.c_str(), outputPath.c_str())) {
    LOG_ERR(config.logTag, "Failed to commit %s -> %s", partPath.c_str(), outputPath.c_str());
    SdMan.remove(partPath.c_str());
    return false;
  }

  LOG_INF(config.logTag, "Converted %s to BMP: %s", converter->formatName(), outputPath.c_str());

  // Stack headroom probe: image conversion (PNG/JPEG decode) is the deepest call chain
  // run on loopTask. Report remaining stack so a future regression shows up as a shrinking
  // high-water mark instead of a mystery "Stack protection fault" crash. Everything is a
  // LOG_DBG argument, so it compiles to nothing at LOG_LEVEL<2 (release).
  LOG_DBG(config.logTag, "Stack headroom: %u bytes (%s)",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)), pcTaskGetName(nullptr));
  return true;
}

bool ImageConverterFactory::isSupported(const std::string& filePath) { return FsHelpers::isImageFile(filePath); }
