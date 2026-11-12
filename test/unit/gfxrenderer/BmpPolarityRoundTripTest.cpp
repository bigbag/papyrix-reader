// Printout BMP polarity round trip. The printer writes 1-bpp files with
// palette 0 = black, 1 = white, and sets a bit for every white pixel. This
// test pins that layout at the reader: a file built byte for byte like
// PrinterApp::saveFramebufferAsBmp builds it must read back as white where
// bits are set and black where they are clear.

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "test_utils.h"

namespace {

template <typename T>
void append(std::string& data, const T value) {
  data.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

// Mirrors PrinterApp::saveFramebufferAsBmp for a 16x2 image: 14-byte file
// header, 40-byte info header, palette 0 = black / 1 = white, bottom-up
// rows padded to 4 bytes.
std::string makePrintoutBmp(uint8_t row0, uint8_t row1) {
  constexpr int32_t width = 16;
  constexpr int32_t height = 2;
  constexpr uint32_t rowPadded = 4;
  const uint32_t pixelBytes = rowPadded * height;
  const uint32_t dataOffset = 14 + 40 + 8;
  const uint32_t fileSize = dataOffset + pixelBytes;

  std::string data;
  data += "BM";
  append(data, fileSize);
  append(data, uint32_t{0});
  append(data, dataOffset);
  append(data, uint32_t{40});
  append(data, width);
  append(data, height);
  append(data, uint16_t{1});  // planes
  append(data, uint16_t{1});  // bits per pixel
  append(data, uint32_t{0});  // BI_RGB
  append(data, pixelBytes);
  append(data, uint32_t{2835});  // 72 dpi
  append(data, uint32_t{2835});
  append(data, uint32_t{2});
  append(data, uint32_t{2});
  data += std::string("\0\0\0\0", 4);        // palette 0: black
  data += std::string("\377\377\377\0", 4);  // palette 1: white
  data.push_back(static_cast<char>(row1));   // bottom row first
  data.append(3, '\0');
  data.push_back(static_cast<char>(row0));
  data.append(3, '\0');
  return data;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("BmpPolarityRoundTripTest");

  // Top row 0b10000000: leftmost pixel white, rest black.
  // Bottom row 0b01111111: leftmost black, rest white.
  SdMan.clearFiles();
  SdMan.registerFile("/printout.bmp", makePrintoutBmp(0x80, 0x7F));
  FsFile file = SdMan.open("/printout.bmp", O_RDONLY);
  Bitmap bitmap(file);
  runner.expectTrue(bitmap.parseHeaders() == BmpReaderError::Ok, "roundtrip: headers parse");
  runner.expectEq<uint16_t>(1, bitmap.getBpp(), "roundtrip: 1 bpp");
  runner.expectEq<int>(16, bitmap.getWidth(), "roundtrip: width");
  runner.expectEq<int>(2, bitmap.getHeight(), "roundtrip: height");

  uint8_t rowBuffer[4] = {};
  uint8_t gray[16] = {};
  runner.expectTrue(bitmap.readGrayscaleRow(gray, sizeof(gray), rowBuffer, sizeof(rowBuffer), 0) == BmpReaderError::Ok,
                    "roundtrip: top row read");
  runner.expectEq<int>(255, gray[0], "roundtrip: set bit reads white");
  runner.expectEq<int>(0, gray[1], "roundtrip: clear bit reads black");
  runner.expectEq<int>(0, gray[15], "roundtrip: clear bit reads black (row end)");

  runner.expectTrue(bitmap.readGrayscaleRow(gray, sizeof(gray), rowBuffer, sizeof(rowBuffer), 1) == BmpReaderError::Ok,
                    "roundtrip: bottom row read");
  runner.expectEq<int>(0, gray[0], "roundtrip: clear bit reads black (row 2)");
  runner.expectEq<int>(255, gray[1], "roundtrip: set bit reads white (row 2)");
  runner.expectEq<int>(255, gray[7], "roundtrip: set bit reads white (row 2 end)");
  runner.expectEq<int>(0, gray[8], "roundtrip: second data byte reads black");

  file.close();

  // Production draw path against both framebuffer grounds. The panel buffer
  // maps back to logical coordinates through panelToLogical.
  {
    papyrix::hal::Display display(8, 10, 21, 4, 5, 6);
    GfxRenderer renderer(display);
    renderer.begin();
    SdMan.clearFiles();
    SdMan.registerFile("/printout.bmp", makePrintoutBmp(0x80, 0x7F));
    FsFile f2 = SdMan.open("/printout.bmp", O_RDONLY);
    Bitmap bmp(f2);
    runner.expectTrue(bmp.parseHeaders() == BmpReaderError::Ok, "draw: headers parse");
    runner.expectTrue(renderer.drawBitmapStreamed(bmp, 0, 0), "draw: streamed path taken");

    // Rebuild the logical image from the panel framebuffer.
    const int panelW = display.getDisplayWidth();
    const int panelH = display.getDisplayHeight();
    const int stride = display.getDisplayWidthBytes();
    const uint8_t* fb = renderer.getFrameBuffer();
    auto logicalWhite = [&](int lx, int ly) {
      for (int py = 0; py < panelH; py++) {
        for (int px = 0; px < panelW; px++) {
          int mx = -1, my = -1;
          if (renderer.panelToLogical(px, py, &mx, &my) && mx == lx && my == ly) {
            return (fb[py * stride + (px >> 3)] & (0x80 >> (px & 7))) != 0;
          }
        }
      }
      return false;
    };
    // Top row: white, black, ...; explicit white ground must cover the rest.
    runner.expectTrue(logicalWhite(0, 0), "draw: set bit stays white");
    runner.expectTrue(!logicalWhite(1, 0), "draw: clear bit drawn black");
    runner.expectTrue(logicalWhite(7, 1), "draw: set bit white (row 2)");
    runner.expectTrue(!logicalWhite(8, 1), "draw: second data byte stays black");
    runner.expectTrue(!logicalWhite(0, 1), "draw: clear bit black (row 2)");
    // Theme check: ground under the image is white even on a black screen.
    renderer.clearScreen(0x00);
    runner.expectTrue(renderer.drawBitmapStreamed(bmp, 0, 0), "draw: second pass");
    runner.expectTrue(logicalWhite(1, 1), "draw: white ground on black theme");
    runner.expectTrue(!logicalWhite(1, 0), "draw: black ink on black theme");
    // Adjacency on black ground, streamed path: the masked ground must stop
    // at the rect edge even in bytes it shares with the panel.
    runner.expectTrue(!logicalWhite(0, 2), "draw: streamed row below stays black");
    renderer.clearScreen(0x00);
    runner.expectTrue(renderer.drawBitmapStreamed(bmp, 4, 1), "draw: offset streamed pass");
    runner.expectTrue(!logicalWhite(3, 1), "draw: streamed left neighbor stays black");
    runner.expectTrue(!logicalWhite(20, 1), "draw: streamed right neighbor stays black");
    runner.expectTrue(!logicalWhite(4, 0), "draw: streamed top neighbor stays black");
    f2.close();
  }

  {
    // Viewer path: the scaled BW draw paints only ink. On a black screen the
    // white ground must come from drawBitmapOnWhite.
    papyrix::hal::Display display(8, 10, 21, 4, 5, 6);
    GfxRenderer renderer(display);
    renderer.begin();
    SdMan.clearFiles();
    SdMan.registerFile("/printout.bmp", makePrintoutBmp(0x80, 0x7F));
    FsFile f3 = SdMan.open("/printout.bmp", O_RDONLY);
    Bitmap bmp(f3);
    runner.expectTrue(bmp.parseHeaders() == BmpReaderError::Ok, "viewer: headers parse");

    const int panelW = display.getDisplayWidth();
    const int panelH = display.getDisplayHeight();
    const int stride = display.getDisplayWidthBytes();
    auto logicalWhite = [&](int lx, int ly) {
      const uint8_t* fb = renderer.getFrameBuffer();
      for (int py = 0; py < panelH; py++) {
        for (int px = 0; px < panelW; px++) {
          int mx = -1, my = -1;
          if (renderer.panelToLogical(px, py, &mx, &my) && mx == lx && my == ly) {
            return (fb[py * stride + (px >> 3)] & (0x80 >> (px & 7))) != 0;
          }
        }
      }
      return false;
    };

    renderer.clearScreen(0xFF);  // light theme analog
    renderer.drawBitmapOnWhite(bmp, 0, 0, 16, 2);
    runner.expectTrue(logicalWhite(0, 0), "viewer: white pixel stays white on white ground");
    runner.expectTrue(!logicalWhite(1, 0), "viewer: ink black on white ground");

    renderer.clearScreen(0x00);  // dark theme analog
    renderer.drawBitmapOnWhite(bmp, 0, 0, 16, 2);
    runner.expectTrue(logicalWhite(0, 0), "viewer: white pixel stays white");
    runner.expectTrue(!logicalWhite(1, 0), "viewer: ink black on black theme");
    runner.expectTrue(logicalWhite(1, 1), "viewer: white ground on black theme");
    runner.expectTrue(!logicalWhite(0, 1), "viewer: row 2 ink black");
    runner.expectTrue(logicalWhite(7, 1), "viewer: row 2 white ground");
    // Outside the image rect the black background stays black, including
    // the pixels sharing framebuffer bytes with the rect edges.
    runner.expectTrue(!logicalWhite(15, 15), "viewer: background untouched");
    bool rowBelowBlack = true;
    for (int lx = 0; lx < 16; lx++) rowBelowBlack = rowBelowBlack && !logicalWhite(lx, 2);
    runner.expectTrue(rowBelowBlack, "viewer: row below rect stays black");
    // Offsets exercise partial bytes on both axes.
    renderer.clearScreen(0x00);
    renderer.drawBitmapOnWhite(bmp, 4, 1, 16, 2);
    runner.expectTrue(!logicalWhite(3, 1), "viewer: left neighbor stays black");
    runner.expectTrue(!logicalWhite(4 + 16, 1), "viewer: right neighbor stays black");
    runner.expectTrue(!logicalWhite(4, 0), "viewer: top neighbor stays black");
    // The initial pass and a re-render after a black clear must match byte
    // for byte: the reconstruction becomes the refresh baseline.
    std::vector<uint8_t> first(renderer.getFrameBuffer(), renderer.getFrameBuffer() + display.getBufferSize());
    renderer.freeBitmapRowBuffers();
    renderer.freeBitmapRowBuffers();
    renderer.clearScreen(0x00);
    renderer.drawBitmapOnWhite(bmp, 4, 1, 16, 2);
    runner.expectTrue(memcmp(first.data(), renderer.getFrameBuffer(), first.size()) == 0,
                      "viewer: re-render after buffer release is byte-identical");
    // Oversized limits must not whiten past the image: a 16x2 image with
    // larger limits keeps the surrounding black.
    renderer.clearScreen(0x00);
    renderer.drawBitmapOnWhite(bmp, 100, 100, 200, 200);
    runner.expectTrue(logicalWhite(100, 100), "viewer: oversized limits paint the image");
    runner.expectTrue(!logicalWhite(100 + 16, 100), "viewer: oversized limits stop at image edge");
    runner.expectTrue(!logicalWhite(100, 100 + 2), "viewer: oversized limits stop at image bottom");
    f3.close();
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
