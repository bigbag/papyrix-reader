#include "test_utils.h"

#include <Bitmap.h>
#include <PackedCoverReducer.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace {

template <typename T>
void append(std::string& data, const T value) {
  data.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::string makeBmp(const int32_t width, const int32_t height, const bool reversedPalette = false,
                    const uint8_t fill = 0xAA) {
  const uint32_t rowSize = (static_cast<uint32_t>(width) + 31U) / 32U * 4U;
  const uint32_t dataOffset = 14 + 40 + 8;
  const uint32_t imageSize = rowSize * static_cast<uint32_t>(height);
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

  const uint8_t first = reversedPalette ? 0xFF : 0x00;
  const uint8_t second = reversedPalette ? 0x00 : 0xFF;
  for (const uint8_t level : {first, second}) {
    data.push_back(static_cast<char>(level));
    data.push_back(static_cast<char>(level));
    data.push_back(static_cast<char>(level));
    data.push_back('\0');
  }

  data.append(static_cast<size_t>(rowSize) * static_cast<size_t>(height), static_cast<char>(fill));
  return data;
}

home_thumbnail::ReduceResult reduce(const char* sourcePath, const char* outputPath, const int width, const int height,
                                    const std::function<bool()>& shouldAbort = nullptr) {
  FsFile source = SdMan.open(sourcePath, O_RDONLY);
  if (!source) return home_thumbnail::ReduceResult::Failed;
  Bitmap bitmap(source);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return home_thumbnail::ReduceResult::Failed;
  FsFile output = SdMan.open(outputPath, O_RDWR | O_CREAT | O_TRUNC);
  if (!output) return home_thumbnail::ReduceResult::Failed;
  const auto result = home_thumbnail::reducePackedCover(bitmap, output, width, height, shouldAbort);
  output.close();
  source.close();
  return result;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("PackedCoverReducer");

  SdMan.reset();
  SdMan.registerFile("/cover.bmp", makeBmp(450, 700));
  runner.expectTrue(reduce("/cover.bmp", "/thumb.part", 282, 440) == home_thumbnail::ReduceResult::Ready,
                    "packed reducer succeeds");
  FsFile reducedFile = SdMan.open("/thumb.part", O_RDONLY);
  Bitmap reduced(reducedFile);
  runner.expectTrue(reduced.parseHeaders() == BmpReaderError::Ok && reduced.hasCompletePixelData() &&
                        reduced.getWidth() == 282 && reduced.getHeight() == 440 && reduced.getBpp() == 1 &&
                        reduced.isTopDown(),
                    "packed output is complete and valid");
  reducedFile.close();

  SdMan.reset();
  SdMan.registerFile("/reversed.bmp", makeBmp(8, 2, true, 0x00));
  runner.expectTrue(reduce("/reversed.bmp", "/reversed-thumb.bmp", 4, 1) == home_thumbnail::ReduceResult::Ready,
                    "reversed palette reduction succeeds");
  FsFile reversedFile = SdMan.open("/reversed-thumb.bmp", O_RDONLY);
  Bitmap reversed(reversedFile);
  runner.expectTrue(reversed.parseHeaders() == BmpReaderError::Ok, "reversed palette output parses");
  uint8_t reversedRow[4] = {};
  runner.expectTrue(reversed.readRawRow(reversedRow, sizeof(reversedRow), 0) == BmpReaderError::Ok,
                    "reversed palette output row reads");
  runner.expectEq(uint8_t{0xFF}, reversedRow[0], "palette luminance maps zero source indices to white");
  reversedFile.close();

  SdMan.reset();
  std::string edgeBmp = makeBmp(4, 5, false, 0xFF);
  memset(edgeBmp.data() + 62 + 4 * 4, 0x00, 4);
  SdMan.registerFile("/edge.bmp", edgeBmp);
  runner.expectTrue(reduce("/edge.bmp", "/edge-thumb.bmp", 4, 3) == home_thumbnail::ReduceResult::Ready,
                    "fractional edge reduction succeeds");
  FsFile edgeFile = SdMan.open("/edge-thumb.bmp", O_RDONLY);
  Bitmap edge(edgeFile);
  runner.expectTrue(edge.parseHeaders() == BmpReaderError::Ok, "fractional edge output parses");
  uint8_t edgeRow[4] = {};
  runner.expectTrue(edge.readRawRow(edgeRow, sizeof(edgeRow), 2) == BmpReaderError::Ok,
                    "fractional edge output row reads");
  runner.expectTrue(edgeRow[0] != 0xFF, "final source row contributes to final destination row");
  edgeFile.close();

  SdMan.reset();
  SdMan.registerFile("/cancel.bmp", makeBmp(450, 700));
  int abortChecks = 0;
  const auto cancelled =
      reduce("/cancel.bmp", "/cancel-thumb.bmp", 282, 440, [&abortChecks]() { return ++abortChecks >= 4; });
  runner.expectTrue(cancelled == home_thumbnail::ReduceResult::Cancelled, "reducer reports cancellation");

  SdMan.reset();
  SdMan.registerFile("/oom.bmp", makeBmp(450, 700));
  testSetLargestFreeBlock(1);
  runner.expectTrue(reduce("/oom.bmp", "/oom-thumb.bmp", 282, 440) == home_thumbnail::ReduceResult::Failed,
                    "unsafe reducer allocation is refused");
  testResetLargestFreeBlock();

  return runner.allPassed() ? 0 : 1;
}
