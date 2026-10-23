#include "ProgressiveJpegImageConverter.h"

#if PAPYRIX_CAP_PROGRESSIVE_JPEG
#include <BitmapHelpers.h>
#include <ProgressiveJpeg.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <memory>

namespace {
struct Conversion {
  Print& output;
  const ImageConvertConfig& config;
  GrayscaleRowScaler& scaler;
  uint8_t* packed;
  size_t rowBytes;
  Atkinson1BitDitherer* oneBit;
  AtkinsonDitherer* twoBit;
  uint16_t width;
  uint16_t height;
  uint32_t sourceRows = 0;
  int outputRows = 0;
  std::function<bool(const uint8_t*)> emit = [this](const uint8_t* row) { return emitRow(row); };

  bool emitRow(const uint8_t* row) {
    if (outputRows >= scaler.outHeight() || (config.shouldAbort && config.shouldAbort())) return false;
    memset(packed, 0, rowBytes);
    for (int x = 0; x < scaler.outWidth(); ++x) {
      if (config.oneBit) {
        const uint8_t value =
            oneBit ? oneBit->processPixel(row[x], x) : quantize1bit(adjustPixel(row[x]), x, outputRows);
        packed[x / 8] |= static_cast<uint8_t>(value << (7 - x % 8));
      } else {
        const int adjusted = adjustPixel(row[x]);
        const uint8_t value = twoBit ? twoBit->processPixel(adjusted, x) : quantize(adjusted, x, outputRows);
        packed[x / 4] |= static_cast<uint8_t>(value << (6 - 2 * (x % 4)));
      }
    }
    if (oneBit) oneBit->nextRow();
    if (twoBit) twoBit->nextRow();
    if (output.write(packed, rowBytes) != rowBytes) return false;
    ++outputRows;
    return true;
  }

  static bool consume(void* user, uint16_t y, const uint8_t* gray, uint16_t width, uint16_t rows, uint16_t stride) {
    auto& self = *static_cast<Conversion*>(user);
    if (y != self.sourceRows || width != self.width || stride < width || self.sourceRows > self.height ||
        rows > self.height - self.sourceRows)
      return false;
    for (uint16_t row = 0; row < rows; ++row) {
      if (self.config.shouldAbort && self.config.shouldAbort()) return false;
      // The scaler can finish before the last source row.
      if (self.outputRows < self.scaler.outHeight() && !self.scaler.accumulate(gray + size_t(row) * stride, self.emit))
        return false;
      ++self.sourceRows;
    }
    return true;
  }
};

bool abortConversion(void* user) {
  const auto& config = *static_cast<const ImageConvertConfig*>(user);
  return config.shouldAbort && config.shouldAbort();
}
}  // namespace

bool ProgressiveJpegImageConverter::convert(FsFile& input, Print& output, const ImageConvertConfig& config) {
  if (config.maxWidth <= 0 || config.maxHeight <= 0 || (config.shouldAbort && config.shouldAbort())) return false;
  ProgressiveJpeg::ImageInfo info;
  auto* abortUser = const_cast<ImageConvertConfig*>(&config);
  if (ProgressiveJpeg::probe(input, info, abortConversion, abortUser) != ProgressiveJpeg::Result::Ok) return false;
  const size_t workspaceSize = ProgressiveJpeg::workspaceBytes(info);
  const size_t widthBound = std::min<size_t>(info.width, static_cast<size_t>(config.maxWidth));
  const size_t rowBound = (widthBound * (config.oneBit ? 1 : 2) + 31) / 32 * 4;
  const size_t buffers = widthBound * 9 + (widthBound + 4) * 6 + rowBound +
                         std::max(sizeof(Atkinson1BitDitherer), sizeof(AtkinsonDitherer));
  const size_t required = workspaceSize + buffers;
  constexpr size_t reserve = 20 * 1024;
  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (workspaceSize == 0 || workspaceSize > heap_caps_get_largest_free_block(caps) ||
      workspaceSize > heap_caps_get_free_size(caps) || required > largest || reserve > largest - required)
    return false;
  std::unique_ptr<uint8_t, decltype(&heap_caps_free)> workspace(
      static_cast<uint8_t*>(heap_caps_malloc(workspaceSize, caps)), &heap_caps_free);
  if (!workspace) return false;
  GrayscaleRowScaler scaler(info.width, info.height, config.maxWidth, config.maxHeight);
  if (!scaler.valid()) return false;
  const int width = scaler.outWidth();
  const size_t rowBytes = (size_t(width) * (config.oneBit ? 1 : 2) + 31) / 32 * 4;
  std::unique_ptr<uint8_t[]> packed(new (std::nothrow) uint8_t[rowBytes]);
  if (!packed) return false;
  std::unique_ptr<Atkinson1BitDitherer> oneBit;
  std::unique_ptr<AtkinsonDitherer> twoBit;
  if (config.oneBit) {
    oneBit.reset(new (std::nothrow) Atkinson1BitDitherer(width));
    if (oneBit && !oneBit->valid()) oneBit.reset();
  } else {
    twoBit.reset(new (std::nothrow) AtkinsonDitherer(width));
    if (twoBit && !twoBit->valid()) twoBit.reset();
  }
  if (config.requireDithering && !(config.oneBit ? bool(oneBit) : bool(twoBit))) return false;
  if (!(config.oneBit ? write1BitBmpHeader(output, width, scaler.outHeight())
                      : write2BitBmpHeader(output, width, scaler.outHeight())))
    return false;
  Conversion conversion{output,       config,       scaler,     packed.get(), rowBytes,
                        oneBit.get(), twoBit.get(), info.width, info.height};
  const ProgressiveJpeg::DecodeOptions options{workspace.get(), workspaceSize, abortConversion, abortUser};
  const auto result = ProgressiveJpeg::decode(input, info, options, Conversion::consume, &conversion);
  return result == ProgressiveJpeg::Result::Ok && conversion.sourceRows == info.height &&
         conversion.outputRows == scaler.outHeight();
}
#endif
