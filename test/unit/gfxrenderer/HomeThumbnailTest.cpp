#include "test_utils.h"

#include <HomeThumbnail.h>
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

std::string readFile(const char* path) {
  FsFile file = SdMan.open(path, O_RDONLY);
  if (!file) return {};
  std::string data(static_cast<size_t>(file.size()), '\0');
  const size_t bytesRead = file.read(data.data(), data.size());
  file.close();
  data.resize(bytesRead);
  return data;
}

std::string makeBmp(const int32_t width, const int32_t height, const uint16_t bpp, const bool topDown) {
  const uint32_t rowSize = (static_cast<uint32_t>(width) * bpp + 31U) / 32U * 4U;
  const uint32_t paletteEntries = bpp == 1 ? 2 : (bpp == 2 ? 4 : 0);
  const uint32_t dataOffset = 14 + 40 + paletteEntries * 4;
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
  append(data, topDown ? -height : height);
  append(data, uint16_t{1});
  append(data, bpp);
  append(data, uint32_t{0});
  append(data, imageSize);
  append(data, int32_t{2835});
  append(data, int32_t{2835});
  append(data, paletteEntries);
  append(data, paletteEntries);
  for (uint32_t i = 0; i < paletteEntries; ++i) {
    const uint8_t level = static_cast<uint8_t>(i * 255U / (paletteEntries - 1));
    data.push_back(static_cast<char>(level));
    data.push_back(static_cast<char>(level));
    data.push_back(static_cast<char>(level));
    data.push_back('\0');
  }

  std::string row(rowSize, '\0');
  for (int32_t y = 0; y < height; ++y) {
    if (bpp == 1) {
      memset(row.data(), y % 2 == 0 ? 0xAA : 0x55, rowSize);
    } else if (bpp == 24) {
      memset(row.data(), 0, rowSize);
      for (int32_t x = 0; x < width; ++x) {
        const uint8_t level = static_cast<uint8_t>((x + y) & 0xFF);
        row[static_cast<size_t>(x) * 3] = static_cast<char>(level);
        row[static_cast<size_t>(x) * 3 + 1] = static_cast<char>(level);
        row[static_cast<size_t>(x) * 3 + 2] = static_cast<char>(level);
      }
    }
    data += row;
  }
  return data;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("HomeThumbnail");
  SdMan.reset();

  runner.expectTrue(home_thumbnail::pathForCache("/cache") == "/cache/thumb.bmp",
                    "legacy thumbnail path is canonical");
  runner.expectEq(320, home_thumbnail::MAX_WIDTH, "legacy thumbnail width is restored");
  runner.expectEq(440, home_thumbnail::MAX_HEIGHT, "legacy thumbnail height is restored");
  runner.expectTrue(home_thumbnail::coverPathForCache("/cache") == "/cache/cover.bmp",
                    "canonical cover path is returned");

  SdMan.registerFile("/cache/thumb.bmp", makeBmp(100, 200, 1, true));
  SdMan.registerFile("/cache/cover.bmp", makeBmp(450, 700, 1, true));
  auto selected = home_thumbnail::selectForHome(true, "/cache/thumb.bmp", "/cache/cover.bmp");
  runner.expectTrue(selected.type == home_thumbnail::HomeImageType::Thumbnail &&
                        selected.path == "/cache/thumb.bmp",
                    "valid thumbnail wins over cover");

  SdMan.reset();
  SdMan.registerFile("/cache/cover.bmp", makeBmp(450, 700, 1, true));
  SdMan.registerFile("/cache/.thumb.failed", "");
  selected = home_thumbnail::selectForHome(true, "/cache/thumb.bmp", "/cache/cover.bmp");
  runner.expectTrue(selected.type == home_thumbnail::HomeImageType::Cover && selected.path == "/cache/cover.bmp",
                    "missing thumbnail falls back to cover despite failure marker");

  SdMan.reset();
  SdMan.registerFile("/cache/thumb.bmp", "invalid");
  SdMan.registerFile("/cache/cover.bmp", makeBmp(450, 700, 1, true));
  selected = home_thumbnail::selectForHome(true, "/cache/thumb.bmp", "/cache/cover.bmp");
  runner.expectTrue(selected.type == home_thumbnail::HomeImageType::Cover,
                    "invalid thumbnail falls back to cover");

  SdMan.reset();
  selected = home_thumbnail::selectForHome(true, "/cache/thumb.bmp", "/cache/cover.bmp");
  runner.expectTrue(selected.type == home_thumbnail::HomeImageType::None && selected.path.empty(),
                    "missing artifacts select placeholder");

  SdMan.registerFile("/cache/thumb.bmp", makeBmp(100, 200, 1, true));
  SdMan.registerFile("/cache/cover.bmp", makeBmp(450, 700, 1, true));
  selected = home_thumbnail::selectForHome(false, "/cache/thumb.bmp", "/cache/cover.bmp");
  runner.expectTrue(selected.type == home_thumbnail::HomeImageType::None && selected.path.empty(),
                    "disabled images select placeholder");

  SdMan.reset();
  SdMan.registerFile("/valid.bmp", makeBmp(320, 440, 1, true));
  home_thumbnail::Info info;
  runner.expectTrue(home_thumbnail::validate("/valid.bmp", &info) && info.width == 320 && info.height == 440,
                    "complete legacy thumbnail is accepted");

  std::string truncated = makeBmp(100, 200, 1, true);
  truncated.resize(truncated.size() - 1);
  SdMan.registerFile("/truncated.bmp", truncated);
  runner.expectFalse(home_thumbnail::validate("/truncated.bmp"), "truncated BMP rejected");

  SdMan.registerFile("/two-bit.bmp", makeBmp(100, 200, 2, true));
  runner.expectFalse(home_thumbnail::validate("/two-bit.bmp"), "wrong bit depth rejected");

  SdMan.registerFile("/too-wide.bmp", makeBmp(321, 440, 1, true));
  runner.expectFalse(home_thumbnail::validate("/too-wide.bmp"), "thumbnail wider than legacy bound is rejected");

  SdMan.registerFile("/too-tall.bmp", makeBmp(320, 441, 1, true));
  runner.expectFalse(home_thumbnail::validate("/too-tall.bmp"), "thumbnail taller than legacy bound is rejected");

  SdMan.registerFile("/bottom-up.bmp", makeBmp(100, 200, 1, false));
  runner.expectFalse(home_thumbnail::validate("/bottom-up.bmp"), "bottom-up thumbnail rejected");

  SdMan.registerFile("/cover-valid.bmp", makeBmp(450, 750, 1, true));
  runner.expectTrue(home_thumbnail::validateCover("/cover-valid.bmp"), "complete 1-bit cover is valid");
  SdMan.registerFile("/cover-two-bit.bmp", makeBmp(450, 750, 2, true));
  runner.expectFalse(home_thumbnail::validateCover("/cover-two-bit.bmp"), "2-bit cover is rejected");
  SdMan.registerFile("/cover-bottom-up.bmp", makeBmp(450, 750, 1, false));
  runner.expectFalse(home_thumbnail::validateCover("/cover-bottom-up.bmp"), "bottom-up cover is rejected");
  std::string shortCover = makeBmp(450, 750, 1, true);
  shortCover.pop_back();
  SdMan.registerFile("/cover-short.bmp", shortCover);
  runner.expectFalse(home_thumbnail::validateCover("/cover-short.bmp"), "truncated cover is rejected");

  const std::string fittingCover = makeBmp(300, 413, 1, true);
  SdMan.reset();
  SdMan.registerFile("/cache/cover.bmp", fittingCover);
  runner.expectTrue(home_thumbnail::generateFromCover("/cache/cover.bmp", "/cache/thumb.bmp") ==
                        home_thumbnail::Result::Ready,
                    "fitting cover publishes thumbnail");
  runner.expectTrue(SdMan.getWrittenData("/cache/thumb.bmp") == fittingCover,
                    "fitting cover is copied byte-for-byte");
  runner.expectFalse(SdMan.exists("/cache/thumb.bmp.tmp") || SdMan.exists("/cache/thumb.bmp.tmp.part"),
                     "successful copy leaves no temporary files");

  SdMan.reset();
  SdMan.registerFile("/cache/cover.bmp", makeBmp(450, 700, 1, true));
  runner.expectTrue(home_thumbnail::generateFromCover("/cache/cover.bmp", "/cache/thumb.bmp") ==
                        home_thumbnail::Result::Ready,
                    "large cover scales successfully");
  home_thumbnail::Info scaled;
  runner.expectTrue(home_thumbnail::validate("/cache/thumb.bmp", &scaled), "scaled thumbnail validates");
  runner.expectTrue(scaled.width == 282 && scaled.height == 440, "large cover preserves aspect ratio");

  SdMan.reset();
  const std::string existing = makeBmp(100, 200, 1, true);
  SdMan.registerFile("/cache/thumb.bmp", existing);
  runner.expectTrue(home_thumbnail::generateFromCover("/missing-cover.bmp", "/cache/thumb.bmp") ==
                        home_thumbnail::Result::Ready,
                    "existing valid thumbnail wins");
  runner.expectTrue(readFile("/cache/thumb.bmp") == existing, "existing thumbnail is not replaced");

  SdMan.reset();
  SdMan.registerFile("/cache/cover.bmp", makeBmp(450, 700, 1, true));
  int abortChecks = 0;
  const auto cancelled = home_thumbnail::generateFromCover(
      "/cache/cover.bmp", "/cache/thumb.bmp", [&abortChecks]() { return ++abortChecks >= 3; });
  runner.expectTrue(cancelled == home_thumbnail::Result::Cancelled, "scaling reports cancellation");
  runner.expectFalse(SdMan.exists("/cache/thumb.bmp"), "cancellation publishes no thumbnail");
  runner.expectFalse(SdMan.exists("/cache/thumb.bmp.tmp"), "cancellation removes temporary thumbnail");
  runner.expectFalse(SdMan.exists("/cache/thumb.bmp.tmp.part"), "cancellation removes partial thumbnail");
  runner.expectFalse(SdMan.exists("/cache/.thumb.failed"), "cancellation creates no failure marker");

  SdMan.reset();
  SdMan.registerFile("/cache/cover.bmp", makeBmp(320, 440, 1, true));
  int copyAbortChecks = 0;
  const auto copyCancelled = home_thumbnail::generateFromCover(
      "/cache/cover.bmp", "/cache/thumb.bmp", [&copyAbortChecks]() { return ++copyAbortChecks >= 3; });
  runner.expectTrue(copyCancelled == home_thumbnail::Result::Cancelled, "copy reports cancellation");
  runner.expectFalse(SdMan.exists("/cache/thumb.bmp") || SdMan.exists("/cache/thumb.bmp.tmp") ||
                         SdMan.exists("/cache/thumb.bmp.tmp.part") || SdMan.exists("/cache/.thumb.failed"),
                     "cancelled copy leaves no artifact or marker");

  SdMan.reset();
  SdMan.registerFile("/cache/thumb.bmp", makeBmp(100, 100, 1, true));
  SdMan.registerFile("/cache/.thumb.failed", "");
  runner.expectTrue(home_thumbnail::generateFromCover("/missing.bmp", "/cache/thumb.bmp") ==
                        home_thumbnail::Result::Ready,
                    "valid thumbnail wins over stale marker");
  runner.expectFalse(SdMan.exists("/cache/.thumb.failed"), "valid thumbnail removes stale marker");

  SdMan.reset();
  SdMan.registerFile("/cache/cover.bmp", "not-a-bmp");
  runner.expectTrue(home_thumbnail::generateFromCover("/cache/cover.bmp", "/cache/thumb.bmp") ==
                        home_thumbnail::Result::Unavailable,
                    "genuine cover error is unavailable");
  runner.expectTrue(SdMan.exists("/cache/.thumb.failed"), "genuine failure creates marker");
  SdMan.registerFile("/cache/cover.bmp", makeBmp(100, 100, 1, true));
  runner.expectTrue(home_thumbnail::generateFromCover("/cache/cover.bmp", "/cache/thumb.bmp") ==
                        home_thumbnail::Result::Unavailable,
                    "failure marker suppresses repeated generation");

  SdMan.reset();
  SdMan.registerFile("/cache/cover.bmp", makeBmp(450, 700, 1, true));
  testSetLargestFreeBlock(1);
  runner.expectTrue(home_thumbnail::generateFromCover("/cache/cover.bmp", "/cache/thumb.bmp") ==
                        home_thumbnail::Result::Unavailable,
                    "unsafe scaling allocation is refused");
  testResetLargestFreeBlock();
  runner.expectFalse(SdMan.exists("/cache/thumb.bmp") || SdMan.exists("/cache/thumb.bmp.tmp") ||
                         SdMan.exists("/cache/thumb.bmp.tmp.part"),
                     "allocation refusal publishes no thumbnail");
  runner.expectTrue(SdMan.exists("/cache/.thumb.failed"), "allocation refusal records genuine failure");

  return runner.allPassed() ? 0 : 1;
}
