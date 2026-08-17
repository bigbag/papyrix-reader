#include "ImageConverter.h"

#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>

#define TAG "IMG_CONV"
#include <PngToBmpConverter.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

namespace {

class JpegImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    if (config.maxWidth == 450 && config.maxHeight == 750 && !config.shouldAbort) {
      return config.oneBit ? JpegToBmpConverter::jpegFileTo1BitBmpStream(input, output)
                           : JpegToBmpConverter::jpegFileToBmpStream(input, output);
    }
    return config.oneBit
               ? JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight,
                                                                     config.shouldAbort, config.requireDithering)
               : JpegToBmpConverter::jpegFileToBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight,
                                                                 config.shouldAbort);
  }

  const char* formatName() const override { return "JPEG"; }
};

class PngImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    return PngToBmpConverter::pngFileToBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight,
                                                         config.oneBit, config.requireDithering, config.shouldAbort);
  }

  const char* formatName() const override { return "PNG"; }
};

class BmpImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    uint8_t buffer[512];
    while (input.available()) {
      if (config.shouldAbort && config.shouldAbort()) return false;
      const int bytesRead = input.read(buffer, sizeof(buffer));
      if (bytesRead <= 0 || output.write(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
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

ImageFormat detectFormat(FsFile& file) {
  const auto originalPosition = file.position();
  if (!file.seek(0)) return ImageFormat::Unknown;

  uint8_t signature[8] = {};
  const int count = file.read(signature, sizeof(signature));
  const bool restored = file.seek(originalPosition);
  if (count < 0 || !restored) return ImageFormat::Unknown;

  if (count >= 2 && signature[0] == 0xFF && signature[1] == 0xD8) return ImageFormat::Jpeg;

  static const uint8_t pngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (count >= static_cast<int>(sizeof(pngSignature)) && memcmp(signature, pngSignature, sizeof(pngSignature)) == 0) {
    return ImageFormat::Png;
  }

  if (count >= 2 && signature[0] == 'B' && signature[1] == 'M') return ImageFormat::Bmp;
  return ImageFormat::Unknown;
}

ImageConverter* converterForFormat(const ImageFormat format) {
  switch (format) {
    case ImageFormat::Jpeg:
      return &jpegConverter;
    case ImageFormat::Png:
      return &pngConverter;
    case ImageFormat::Bmp:
      return &bmpConverter;
    case ImageFormat::Unknown:
      return nullptr;
  }
  return nullptr;
}

}  // namespace

ImageFormat ImageConverterFactory::detectFormat(const std::string& filePath) {
  FsFile file;
  if (!SdMan.openFileForRead(TAG, filePath, file)) return ImageFormat::Unknown;
  const ImageFormat format = ::detectFormat(file);
  file.close();
  return format;
}

bool ImageConverterFactory::convertToBmp(const std::string& inputPath, const std::string& outputPath,
                                         const ImageConvertConfig& config) {
  // Stack safety gate: PNG/JPEG decode (pngle + zlib/tinflate) is the deepest call chain in
  // the reader and can overflow a constrained task stack, panicking the whole device. If the
  // current task's free stack is below the safety floor, skip this image gracefully instead —
  // callers can skip or retry the asset rather than rebooting. The 12 KB foreground and Reader
  // background task stacks keep this gate from triggering in normal use; it only fires when the stack is genuinely
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

  ImageConverter* converter = converterForFormat(::detectFormat(inputFile));
  if (!converter) {
    inputFile.close();
    LOG_ERR(config.logTag, "Unsupported image format: %s", inputPath.c_str());
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

  if (config.validateOutput && !config.validateOutput(partPath)) {
    LOG_ERR(config.logTag, "Converted BMP failed validation: %s", partPath.c_str());
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

  // Stack headroom probe: image conversion (PNG/JPEG decode) is the deepest call chain.
  // Report remaining stack so a future regression shows up as a shrinking
  // high-water mark instead of a mystery "Stack protection fault" crash. Everything is a
  // LOG_DBG argument, so it compiles to nothing at LOG_LEVEL<2 (release).
  LOG_DBG(config.logTag, "Stack headroom: %u bytes (%s)",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)), pcTaskGetName(nullptr));
  return true;
}

bool ImageConverterFactory::isSupported(const std::string& filePath) { return FsHelpers::isImageFile(filePath); }
