#include <BitmapHelpers.h>
#include <ProgressiveJpeg.h>
#include <ProgressiveJpegImageConverter.h>
#include <esp_heap_caps.h>

#include <cassert>
#include <vector>

#include "test_utils.h"

namespace {
std::string fixture(const char* name) { return TestUtils::readFile(std::string(PROGRESSIVE_FIXTURES) + "/" + name); }
uint32_t word(const std::string& bytes, size_t offset) {
  assert(offset + 4 <= bytes.size());
  return uint8_t(bytes[offset]) | (uint32_t(uint8_t(bytes[offset + 1])) << 8) |
         (uint32_t(uint8_t(bytes[offset + 2])) << 16) | (uint32_t(uint8_t(bytes[offset + 3])) << 24);
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("ProgressiveJpegConverter");
  ProgressiveJpegImageConverter converter;
  const std::string bytes = fixture("color-420.jpg");
  FsFile input;
  input.setBuffer(bytes);
  ProgressiveJpeg::ImageInfo info;
  assert(ProgressiveJpeg::probe(input, info) == ProgressiveJpeg::Result::Ok);
  std::vector<uint8_t> workspace(ProgressiveJpeg::workspaceBytes(info)), gray;
  auto collect = [](void* user, uint16_t, const uint8_t* data, uint16_t width, uint16_t rows, uint16_t stride) {
    auto& pixels = *static_cast<std::vector<uint8_t>*>(user);
    for (unsigned row = 0; row < rows; ++row)
      pixels.insert(pixels.end(), data + row * stride, data + row * stride + width);
    return true;
  };
  assert(ProgressiveJpeg::decode(input, info, {workspace.data(), workspace.size()}, collect, &gray) ==
         ProgressiveJpeg::Result::Ok);
  for (bool oneBit : {false, true}) {
    for (int bound : {100, 31, 1}) {
      ImageConvertConfig config;
      config.oneBit = oneBit;
      config.maxWidth = bound;
      config.maxHeight = bound;
      config.requireDithering = true;
      FsFile output;
      output.setBuffer("");
      input.setBuffer(bytes);
      const size_t freed = testHeapCapsFreeCount();
      runner.expectTrue(converter.convert(input, output, config), "progressive conversion succeeds");
      runner.expectEq(freed + 1, testHeapCapsFreeCount(), "conversion releases decoder workspace");
      const std::string actual = output.getBuffer();
      GrayscaleRowScaler scaler(info.width, info.height, bound, bound);
      const int width = scaler.outWidth(), height = scaler.outHeight();
      const size_t stride = (size_t(width) * (oneBit ? 1 : 2) + 31) / 32 * 4;
      runner.expectEq(uint32_t(width), word(actual, 18), "scaled BMP width");
      runner.expectEq(uint32_t(-height), word(actual, 22), "top-down BMP height");
      runner.expectEq(unsigned(oneBit ? 1 : 2), unsigned(uint8_t(actual[28])), "BMP bit depth");
      const size_t offset = word(actual, 10);
      runner.expectEq(offset + stride * height, actual.size(), "complete BMP rows");
      Atkinson1BitDitherer dither1(width);
      AtkinsonDitherer dither2(width);
      std::string expected;
      int rowCount = 0;
      for (int y = 0; y < info.height && rowCount < height; ++y) {
        assert(scaler.accumulate(gray.data() + y * info.width, [&](const uint8_t* row) {
          std::string packed(stride, '\0');
          for (int x = 0; x < width; ++x) {
            const uint8_t value =
                oneBit ? dither1.processPixel(row[x], x) : dither2.processPixel(adjustPixel(row[x]), x);
            const int shift = oneBit ? 7 - x % 8 : 6 - 2 * (x % 4);
            packed[x / (oneBit ? 8 : 4)] |= char(value << shift);
          }
          expected += packed;
          dither1.nextRow();
          dither2.nextRow();
          ++rowCount;
          return true;
        }));
      }
      runner.expectEq(expected, actual.substr(offset), "packed pixels and padding match the row pipeline");
      for (const char* name : {"black.jpg", "white.jpg"}) {
        input.setBuffer(fixture(name));
        output.setBuffer("");
        assert(converter.convert(input, output, config));
        const std::string bmp = output.getBuffer();
        const size_t w = word(bmp, 18), h = -int32_t(word(bmp, 22));
        const size_t rowSize = (w * (oneBit ? 1 : 2) + 31) / 32 * 4;
        bool correct = true;
        for (size_t y = 0; y < h; ++y)
          for (size_t x = 0; x < w; ++x) {
            const uint8_t packed = uint8_t(bmp[word(bmp, 10) + y * rowSize + x / (oneBit ? 8 : 4)]);
            const unsigned value = (packed >> (oneBit ? 7 - x % 8 : 6 - 2 * (x % 4))) & (oneBit ? 1 : 3);
            correct &= value == (name[0] == 'b' ? 0U : oneBit ? 1U : 3U);
          }
        runner.expectTrue(correct, "constant image has independently known packed pixels");
      }
    }
  }
  for (bool oneBit : {false, true}) {
    ImageConvertConfig tiny;
    tiny.maxWidth = tiny.maxHeight = 1;
    tiny.oneBit = oneBit;
    FsFile reduced;
    reduced.setBuffer("");
    input.setBuffer(fixture("gray-maximum.jpg"));
    runner.expectTrue(converter.convert(input, reduced, tiny), "maximum image reduces to one pixel");
    const std::string bmp = reduced.getBuffer();
    const size_t offset = word(bmp, 10);
    runner.expectEq(uint32_t(1), word(bmp, 18), "extreme reduction width");
    runner.expectEq(unsigned(128), unsigned(uint8_t(bmp[offset])), "extreme reduction retains gray");
  }
  ImageConvertConfig config;
  FsFile output;
  auto attempt = [&]() {
    input.setBuffer(bytes);
    output.setBuffer("");
    return converter.convert(input, output, config);
  };
  testSetHeapPool(MALLOC_CAP_INTERNAL, 400000, 400000);
  testSetHeapPool(MALLOC_CAP_SPIRAM, 0, 0);
  runner.expectFalse(attempt(), "absent PSRAM rejects conversion");
  runner.expectTrue(output.getBuffer().empty(), "admission failure writes no output");
  testSetHeapPool(MALLOC_CAP_SPIRAM, 400000, 400000);
  testSetPsramAllocationFailure(true);
  runner.expectFalse(attempt(), "PSRAM refusal has no internal fallback");
  testResetLargestFreeBlock();
  runner.expectTrue(attempt(), "conversion recovers after allocation refusal");
  input.setBuffer(bytes);
  output.setBuffer("");
  output.setWriteLimit(70);
  size_t freed = testHeapCapsFreeCount();
  runner.expectFalse(converter.convert(input, output, config), "short row write fails");
  runner.expectEq(freed + 1, testHeapCapsFreeCount(), "write failure releases workspace");
  input.setBuffer(bytes.substr(0, bytes.size() - 2));
  output.setBuffer("");
  runner.expectFalse(converter.convert(input, output, config), "malformed entropy cannot produce success");
  config.shouldAbort = [] { return true; };
  runner.expectFalse(attempt(), "cancellation fails conversion");
  config.shouldAbort = nullptr;
  runner.expectTrue(attempt(), "conversion recovers after errors");
  return runner.allPassed() ? 0 : 1;
}
