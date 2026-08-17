#include "test_utils.h"

#include <Bitmap.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <cstdint>
#include <string>

namespace {

template <typename T>
void append(std::string& data, const T value) {
  data.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::string makeTopDown1BitBmp(const int32_t height) {
  constexpr int32_t width = 8;
  constexpr uint32_t rowSize = 4;
  const uint32_t imageSize = rowSize * static_cast<uint32_t>(height);
  constexpr uint32_t dataOffset = 14 + 40 + 8;
  const uint32_t fileSize = dataOffset + imageSize;

  std::string data;
  data.reserve(fileSize);
  data += "BM";
  append(data, fileSize);
  append(data, uint32_t{0});
  append(data, dataOffset);
  append(data, uint32_t{40});
  append(data, width);
  append(data, -height);
  append(data, uint16_t{1});
  append(data, uint16_t{1});
  append(data, uint32_t{0});
  append(data, imageSize);
  append(data, int32_t{2835});
  append(data, int32_t{2835});
  append(data, uint32_t{2});
  append(data, uint32_t{2});
  append(data, uint32_t{0x00000000});
  append(data, uint32_t{0x00FFFFFF});
  for (int32_t row = 0; row < height; ++row) {
    data.push_back(static_cast<char>(row + 1 == height ? 0xFF : 0x00));
    data.append(rowSize - 1, '\0');
  }
  return data;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("BitmapRowAccess");

  constexpr int32_t kHeight = 300;
  SdMan.clearFiles();
  SdMan.registerFile("/cover.bmp", makeTopDown1BitBmp(kHeight));
  FsFile file = SdMan.open("/cover.bmp", O_RDONLY);
  Bitmap bitmap(file);
  runner.expectTrue(bitmap.parseHeaders() == BmpReaderError::Ok, "headers_parse");
  runner.expectTrue(bitmap.hasCompletePixelData(), "complete pixel span accepted");

  const std::string truncatedData = makeTopDown1BitBmp(2).substr(0, makeTopDown1BitBmp(2).size() - 1);
  SdMan.registerFile("/truncated.bmp", truncatedData);
  FsFile truncatedFile = SdMan.open("/truncated.bmp", O_RDONLY);
  Bitmap truncated(truncatedFile);
  runner.expectTrue(truncated.parseHeaders() == BmpReaderError::Ok && !truncated.hasCompletePixelData(),
                    "truncated final row rejected");

  std::string implicitPaletteData = makeTopDown1BitBmp(1);
  memset(implicitPaletteData.data() + 46, 0, 4);
  memset(implicitPaletteData.data() + 54, 0xFF, 3);
  implicitPaletteData[57] = 0;
  memset(implicitPaletteData.data() + 58, 0, 4);
  SdMan.registerFile("/implicit-palette.bmp", implicitPaletteData);
  FsFile implicitPaletteFile = SdMan.open("/implicit-palette.bmp", O_RDONLY);
  Bitmap implicitPalette(implicitPaletteFile);
  uint8_t grayscale[8] = {};
  uint8_t rawPaletteRow[4] = {};
  runner.expectTrue(implicitPalette.parseHeaders() == BmpReaderError::Ok &&
                        implicitPalette.readGrayscaleRow(grayscale, sizeof(grayscale), rawPaletteRow,
                                                         sizeof(rawPaletteRow), 0) == BmpReaderError::Ok &&
                        grayscale[0] == 0,
                    "colorsUsed zero reads the implicit indexed palette");

  testSetLargestFreeBlock(4);
  runner.expectFalse(bitmap.preloadAllRows(), "unsafe_preload_declined");

  uint8_t output[2] = {};
  uint8_t source[4] = {};
  runner.expectTrue(bitmap.readRow(output, source, kHeight - 1) == BmpReaderError::Ok, "random_row_read");
  runner.expectTrue(output[0] == 0xFF && output[1] == 0xFF, "requested_row_is_read");
  testResetLargestFreeBlock();

  return runner.allPassed() ? 0 : 1;
}
