#include "ImageConverter.h"

#include <Bitmap.h>
#include <BitmapHelpers.h>
#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#define TAG "IMG_CONV"
#include <PngToBmpConverter.h>
#include <SDCardManager.h>
#include <TargetConfig.h>
#if PAPYRIX_CAP_PROGRESSIVE_JPEG
#include "ProgressiveJpegImageConverter.h"
#endif
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstring>
#include <memory>

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
    Bitmap bitmap(input);
    if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
      LOG_ERR(config.logTag, "Invalid BMP input");
      return false;
    }

    GrayscaleRowScaler scaler(bitmap.getWidth(), bitmap.getHeight(), config.maxWidth, config.maxHeight);
    if (!scaler.valid()) {
      LOG_ERR(config.logTag, "Failed to prepare BMP scaler");
      return false;
    }
    const int outW = scaler.outWidth();
    const int outH = scaler.outHeight();
    const int outRowBytes = config.oneBit ? (outW + 31) / 32 * 4 : (outW * 2 + 31) / 32 * 4;
    const size_t rawRowBytes = static_cast<size_t>(bitmap.getRowBytes());
    const size_t grayBytes = static_cast<size_t>(bitmap.getWidth());
    const size_t required = grayBytes + rawRowBytes + static_cast<size_t>(outRowBytes) +
                            (scaler.needsScaling() ? static_cast<size_t>(outW) * 9 : 0);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (required > 1024 && required > largest * 80 / 100) {
      LOG_ERR(config.logTag, "Insufficient heap for BMP conversion");
      return false;
    }

    std::unique_ptr<uint8_t[]> grayRow(new (std::nothrow) uint8_t[grayBytes]());
    std::unique_ptr<uint8_t[]> rawRow(new (std::nothrow) uint8_t[rawRowBytes]());
    std::unique_ptr<uint8_t[]> outRow(new (std::nothrow) uint8_t[outRowBytes]());
    if (!grayRow || !rawRow || !outRow) {
      LOG_ERR(config.logTag, "Failed to allocate BMP row buffers");
      return false;
    }

    std::unique_ptr<Atkinson1BitDitherer> oneBitDitherer;
    std::unique_ptr<AtkinsonDitherer> ditherer;
    if (config.oneBit) {
      oneBitDitherer.reset(new (std::nothrow) Atkinson1BitDitherer(outW));
      if (oneBitDitherer && !oneBitDitherer->valid()) oneBitDitherer.reset();
      if (config.requireDithering && !oneBitDitherer) {
        LOG_ERR(config.logTag, "Failed to allocate 1-bit ditherer");
        return false;
      }
    } else {
      ditherer.reset(new (std::nothrow) AtkinsonDitherer(outW));
      if (ditherer && !ditherer->valid()) ditherer.reset();
    }

    if (!(config.oneBit ? write1BitBmpHeader(output, outW, outH) : write2BitBmpHeader(output, outW, outH))) {
      LOG_ERR(config.logTag, "Failed to write output BMP header");
      return false;
    }

    const int srcH = bitmap.getHeight();
    for (int y = 0; y < srcH; y++) {
      if (config.shouldAbort && config.shouldAbort()) return false;
      if (bitmap.readGrayscaleRow(grayRow.get(), grayBytes, rawRow.get(), rawRowBytes, y) != BmpReaderError::Ok) {
        LOG_ERR(config.logTag, "Failed to read BMP row %d", y);
        return false;
      }
      const bool ok = scaler.accumulate(grayRow.get(), [&](const uint8_t* meanRow) {
        memset(outRow.get(), 0, static_cast<size_t>(outRowBytes));
        for (int x = 0; x < outW; x++) {
          if (config.oneBit) {
            const uint8_t bit = oneBitDitherer ? oneBitDitherer->processPixel(meanRow[x], x)
                                               : quantize1bit(adjustPixel(meanRow[x]), x, y);
            outRow[x >> 3] |= static_cast<uint8_t>(bit << (7 - (x & 7)));
          } else {
            const uint8_t adjusted = static_cast<uint8_t>(adjustPixel(meanRow[x]));
            const uint8_t level = ditherer ? ditherer->processPixel(adjusted, x) : quantize(adjusted, x, y);
            outRow[(x * 2) / 8] |= static_cast<uint8_t>(level << (6 - ((x * 2) % 8)));
          }
        }
        if (oneBitDitherer) oneBitDitherer->nextRow();
        if (ditherer) ditherer->nextRow();
        return output.write(outRow.get(), static_cast<size_t>(outRowBytes)) == static_cast<size_t>(outRowBytes);
      });
      if (!ok) {
        LOG_ERR(config.logTag, "Failed to emit BMP output row");
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

#if PAPYRIX_CAP_PROGRESSIVE_JPEG
ProgressiveJpegImageConverter progressiveConverter;

ImageConverter* selectJpegConverter(FsFile& input, const std::function<bool()>& shouldAbort) {
  const auto entry = input.position();
  auto finish = [&](ImageConverter* converter) { return input.seek(entry) ? converter : nullptr; };
  if (!input.seek(0) || input.read() != 0xFF || input.read() != 0xD8) return finish(nullptr);
  for (;;) {
    if (shouldAbort && shouldAbort()) return finish(nullptr);
    if (input.read() != 0xFF) return finish(nullptr);
    int marker;
    do {
      if (shouldAbort && shouldAbort()) return finish(nullptr);
      marker = input.read();
    } while (marker == 0xFF);
    if (marker < 0 || marker == 0 || marker == 0xD8 || marker == 0xD9 || marker == 0xDA || marker == 0x01 ||
        (marker >= 0xD0 && marker <= 0xD7))
      return finish(nullptr);
    const int hi = input.read(), lo = input.read();
    if (hi < 0 || lo < 0) return finish(nullptr);
    const unsigned length = (hi << 8) | lo;
    const auto position = input.position();
    if (length < 2 || position > input.size() || length - 2 > input.size() - position) return finish(nullptr);
    if (marker == 0xC0 || marker == 0xC1) return finish(&jpegConverter);
    if (marker == 0xC2) return finish(&progressiveConverter);
    if ((marker >= 0xC3 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) || marker == 0xCC)
      return finish(nullptr);
    if (!input.seek(position + length - 2)) return finish(nullptr);
  }
}
#endif

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
  // The watermark retains earlier peaks, not the stack space available now.
  static_assert(portSTACK_GROWTH == -1, "Image stack checks require a downward-growing stack");
  constexpr size_t kMinImageStackBytes = 4096;
  const uintptr_t stackBase = reinterpret_cast<uintptr_t>(pxTaskGetStackStart(nullptr));
  const uintptr_t stackPointer = reinterpret_cast<uintptr_t>(esp_cpu_get_sp());
  const size_t freeStackBytes = stackBase != 0 && stackPointer > stackBase ? stackPointer - stackBase : 0;
  if (freeStackBytes < kMinImageStackBytes) {
    LOG_WRN(config.logTag, "Skip image convert (stack=%zu bytes task=%s): %s", freeStackBytes, pcTaskGetName(nullptr),
            inputPath.c_str());
    return false;
  }

  FsFile inputFile;
  if (!SdMan.openFileForRead(config.logTag, inputPath, inputFile)) {
    LOG_ERR(config.logTag, "Failed to open input file: %s", inputPath.c_str());
    return false;
  }

  const ImageFormat format = ::detectFormat(inputFile);
  ImageConverter* converter = converterForFormat(format);
#if PAPYRIX_CAP_PROGRESSIVE_JPEG
  if (format == ImageFormat::Jpeg) converter = selectJpegConverter(inputFile, config.shouldAbort);
#endif
  if (!converter) {
    inputFile.close();
    LOG_ERR(config.logTag, "Unsupported image format: %s", inputPath.c_str());
    return false;
  }

  // Convert into a temporary file before replacement.
  const std::string partPath = outputPath + ".part";
  SdMan.remove(partPath.c_str());

  FsFile outputFile;
  if (!SdMan.openFileForWrite(config.logTag, partPath, outputFile)) {
    inputFile.close();
    LOG_ERR(config.logTag, "Failed to create output file: %s", partPath.c_str());
    return false;
  }

  const bool success = converter->convert(inputFile, outputFile, config) && outputFile.sync();

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

  LOG_DBG(config.logTag, "Stack headroom: %u bytes (%s)", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
          pcTaskGetName(nullptr));
  return true;
}

bool ImageConverterFactory::isSupported(const std::string& filePath) { return FsHelpers::isImageFile(filePath); }
