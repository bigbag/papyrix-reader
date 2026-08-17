#include "test_utils.h"

#include <ContentHandle.h>
#include <HomeThumbnail.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// Minimal XTC builder (same layout as XtcProviderCoverTest)
// ============================================================================
static std::string buildXtcFile1Bit(uint16_t width, uint16_t height, const std::vector<uint8_t>& pixelData) {
  constexpr size_t headerSize = 56;  // sizeof(xtc::XtcHeader)
  constexpr size_t pageTableOffset = headerSize + 128 + 64;
  const size_t pageDataOffset = pageTableOffset + 16;

  const size_t bitmapSize = ((width + 7) / 8) * static_cast<size_t>(height);
  const size_t pageDataSize = 22 + bitmapSize;

  std::string buf(pageDataOffset + pageDataSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

  auto* hdr = reinterpret_cast<uint32_t*>(data);
  hdr[0] = 0x00435458;  // XTC\0
  data[4] = 1;
  data[5] = 0;
  data[6] = 1;  // pageCount
  data[9] = 1;  // hasMetadata
  memcpy(data + 0x18, &pageTableOffset, 8);
  memcpy(data + 0x20, &pageDataOffset, 8);
  memcpy(data + headerSize, "XTC Book", 8);

  const uint64_t pteOffset = pageTableOffset;
  memcpy(data + pteOffset, &pageDataOffset, 8);
  const uint32_t pteSize = static_cast<uint32_t>(pageDataSize);
  memcpy(data + pteOffset + 8, &pteSize, 4);
  memcpy(data + pteOffset + 12, &width, 2);
  memcpy(data + pteOffset + 14, &height, 2);

  const uint32_t magic = 0x00475458;  // XTG\0
  memcpy(data + pageDataOffset, &magic, 4);
  memcpy(data + pageDataOffset + 4, &width, 2);
  memcpy(data + pageDataOffset + 6, &height, 2);
  memcpy(data + pageDataOffset + 10, &bitmapSize, 4);  // dataSize at 0x0A

  const size_t toCopy = std::min(pixelData.size(), bitmapSize);
  if (toCopy > 0) memcpy(data + pageDataOffset + 22, pixelData.data(), toCopy);
  return buf;
}

// ============================================================================
// Minimal 24-bpp BMP gradient (exercises the canonical BMP converter)
// ============================================================================
static std::string build24bpp(const uint16_t w, const uint16_t h) {
  const uint32_t rowSize = (static_cast<uint32_t>(w) * 3 + 3) / 4 * 4;
  std::string data(14 + 40 + rowSize * h, '\0');
  auto put32 = [&](const size_t off, const uint32_t v) { memcpy(&data[off], &v, 4); };
  data[0] = 'B';
  data[1] = 'M';
  put32(2, static_cast<uint32_t>(data.size()));
  put32(10, 54);
  put32(14, 40);
  put32(18, w);
  put32(22, -static_cast<int32_t>(h));
  memcpy(&data[26], "\x01\x00", 2);
  memcpy(&data[28], "\x18\x00", 2);
  put32(34, rowSize * h);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t y = static_cast<uint16_t>(h - 1 - row);
    for (uint16_t x = 0; x < w; x++) {
      const uint8_t v = static_cast<uint8_t>((x * 255) / (w > 1 ? w - 1 : 1));
      const size_t off = 54 + static_cast<size_t>(row) * rowSize + static_cast<size_t>(x) * 3;
      data[off] = v;
      data[off + 1] = v;
      data[off + 2] = v;
    }
  }
  return data;
}

// ============================================================================
// Stored (uncompressed) ZIP builder for the EPUB fixture
// ============================================================================
namespace {

struct ZipEntry {
  std::string name;
  std::string data;
};

uint32_t crc32Of(const std::string& data) {
  static uint32_t table[256];
  static bool initialized = false;
  if (!initialized) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    initialized = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (const char c : data) crc = table[(crc ^ static_cast<uint8_t>(c)) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

void put16z(std::string& out, const uint16_t v) {
  const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
  out.append(reinterpret_cast<const char*>(b), 2);
}

void put32z(std::string& out, const uint32_t v) {
  const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                        static_cast<uint8_t>(v >> 24)};
  out.append(reinterpret_cast<const char*>(b), 4);
}

std::string buildStoredZip(const std::vector<ZipEntry>& entries) {
  std::string out;
  std::string central;
  for (const ZipEntry& e : entries) {
    const uint32_t crc = crc32Of(e.data);
    const uint32_t size = static_cast<uint32_t>(e.data.size());
    const uint32_t offset = static_cast<uint32_t>(out.size());
    put32z(out, 0x04034b50);
    put16z(out, 20);
    put16z(out, 0);
    put16z(out, 0);  // stored
    put16z(out, 0);
    put16z(out, 0);
    put32z(out, crc);
    put32z(out, size);
    put32z(out, size);
    put16z(out, static_cast<uint16_t>(e.name.size()));
    put16z(out, 0);
    out += e.name;
    out += e.data;

    put32z(central, 0x02014b50);
    put16z(central, 20);
    put16z(central, 20);
    put16z(central, 0);
    put16z(central, 0);
    put16z(central, 0);
    put16z(central, 0);
    put32z(central, crc);
    put32z(central, size);
    put32z(central, size);
    put16z(central, static_cast<uint16_t>(e.name.size()));
    put16z(central, 0);
    put16z(central, 0);
    put16z(central, 0);
    put16z(central, 0);
    put32z(central, 0);
    put32z(central, offset);
    central += e.name;
  }
  const uint32_t cdOffset = static_cast<uint32_t>(out.size());
  const uint32_t cdSize = static_cast<uint32_t>(central.size());
  out += central;
  put32z(out, 0x06054b50);
  put16z(out, 0);
  put16z(out, 0);
  put16z(out, static_cast<uint16_t>(entries.size()));
  put16z(out, static_cast<uint16_t>(entries.size()));
  put32z(out, cdSize);
  put32z(out, cdOffset);
  put16z(out, 0);
  return out;
}

std::string buildEpub() {
  const std::string container =
      "<?xml version=\"1.0\"?>"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
      "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
  const std::string opf =
      "<?xml version=\"1.0\"?>"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"id\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<dc:title>EPUB Book</dc:title><dc:creator>Author</dc:creator>"
      "<dc:identifier id=\"id\">test-1</dc:identifier>"
      "<meta name=\"cover\" content=\"cover-img\"/>"
      "</metadata>"
      "<manifest>"
      "<item id=\"cover-img\" href=\"cover.bmp\" media-type=\"image/bmp\"/>"
      "<item id=\"ch1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "</manifest>"
      "<spine><itemref idref=\"ch1\"/></spine></package>";
  const std::string chapter =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>c</title></head>"
      "<body><p>Hello EPUB</p></body></html>";

  std::vector<ZipEntry> entries = {
      {"mimetype", "application/epub+zip"},
      {"META-INF/container.xml", container},
      {"OEBPS/content.opf", opf},
      {"OEBPS/ch1.xhtml", chapter},
      {"OEBPS/cover.bmp", build24bpp(40, 20)},
  };
  return buildStoredZip(entries);
}

std::string buildFb2() {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" "
         "xmlns:l=\"http://www.w3.org/1999/xlink\">"
         "<description><title-info><book-title>FB2 Book</book-title></title-info></description>"
         "<body><section><p>Text</p></section></body>"
         "</FictionBook>";
}

}  // namespace

// ============================================================================
// Dispatch scenarios
// ============================================================================
int main() {
  TestUtils::TestRunner runner("ContentHandleThumbnailTest");
  testResetLargestFreeBlock();

  struct Case {
    const char* name;
    const char* path;
    std::string content;
    bool withSidecar;  // register <stem>.bmp sidecar next to the document
  };

  const std::vector<Case> cases = {
      {"txt", "/books/story.txt", "Once upon a time.\n", true},
      {"markdown", "/books/story.md", "# Title\n\nBody text.\n", true},
      {"html", "/books/story.html", "<html><body><p>Hi</p></body></html>", true},
      {"fb2", "/books/story.fb2", buildFb2(), true},
      {"epub", "/books/story.epub", buildEpub(), false},
  };

  for (const Case& c : cases) {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    SdMan.registerFile(c.path, c.content);
    if (c.withSidecar) SdMan.registerFile("/books/cover.bmp", build24bpp(48, 24));

    papyrix::ContentHandle handle;
    const std::string label = std::string("open ") + c.name;
    runner.expectTrue(handle.open(c.path, "/cache").ok(), label.c_str());

    const std::string coverPath = handle.getCoverPath();
    runner.expectTrue(!coverPath.empty(), (std::string(c.name) + ": cover path non-empty").c_str());

    // Pre-cancelled thumbnail request never touches storage (must run before
    // any artifacts exist, otherwise a valid thumbnail short-circuits to Ready)
    runner.expectTrue(handle.generateThumbnail([]() { return true; }) == home_thumbnail::Result::Cancelled,
                      (std::string(c.name) + ": pre-cancelled returns Cancelled").c_str());

    const std::string generated = handle.generateCover(true, nullptr);
    runner.expectTrue(!generated.empty(), (std::string(c.name) + ": cover generated").c_str());
    runner.expectTrue(home_thumbnail::validateCover(coverPath),
                      (std::string(c.name) + ": published cover is valid").c_str());

    const std::string thumbPath = handle.getThumbnailPath();
    runner.expectTrue(!thumbPath.empty(), (std::string(c.name) + ": thumbnail path non-empty").c_str());
    runner.expectTrue(handle.generateThumbnail(nullptr) == home_thumbnail::Result::Ready,
                      (std::string(c.name) + ": thumbnail generated").c_str());
    runner.expectTrue(home_thumbnail::validate(thumbPath),
                      (std::string(c.name) + ": thumbnail is valid").c_str());

    handle.close();
  }

  // ---- XTC: cover derives from page 0 (no sidecar involved) ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    SdMan.registerFile("/books/comic.xtc", buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF)));

    papyrix::ContentHandle handle;
    runner.expectTrue(handle.open("/books/comic.xtc", "/cache").ok(), "open xtc");
    runner.expectTrue(!handle.generateCover(true, nullptr).empty(), "xtc: cover generated from page 0");
    runner.expectTrue(home_thumbnail::validateCover(handle.getCoverPath()), "xtc: cover valid");
    runner.expectTrue(handle.generateThumbnail(nullptr) == home_thumbnail::Result::Ready,
                      "xtc: thumbnail generated");
    handle.close();
  }

  // ---- TXT without any cover source: dispatch reports Unavailable ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    SdMan.registerFile("/books/plain.txt", "No images here.\n");

    papyrix::ContentHandle handle;
    runner.expectTrue(handle.open("/books/plain.txt", "/cache").ok(), "open plain txt");
    runner.expectTrue(handle.generateThumbnail(nullptr) == home_thumbnail::Result::Unavailable,
                      "plain txt: no cover source is Unavailable");
    handle.close();
  }

  return runner.allPassed() ? 0 : 1;
}
