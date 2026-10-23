#include <Bitmap.h>
#include <ContentHandle.h>
#include <CoverHelpers.h>
#include <Epub.h>
#include <HardwareIdentity.h>
#include <HomeThumbnail.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "test_utils.h"

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

// Uniform 24-bpp top-down BMP (canonical BMP converter input).
std::string build24bpp(const uint16_t w, const uint16_t h, const uint8_t gray) {
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
    for (uint16_t x = 0; x < w; x++) {
      const size_t off = 54 + static_cast<size_t>(row) * rowSize + static_cast<size_t>(x) * 3;
      data[off] = gray;
      data[off + 1] = gray;
      data[off + 2] = gray;
    }
  }
  return data;
}

std::string be32(const uint32_t v) {
  const uint8_t b[4] = {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
                        static_cast<uint8_t>(v)};
  return std::string(reinterpret_cast<const char*>(b), 4);
}

uint32_t adler32Of(const std::string& data) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (const char c : data) {
    a = (a + static_cast<uint8_t>(c)) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

void appendPngChunk(std::string& png, const char* type, const std::string& chunkData) {
  png += be32(static_cast<uint32_t>(chunkData.size()));
  png.append(type, 4);
  png += chunkData;
  png += be32(crc32Of(std::string(type, 4) + chunkData));
}

// Uniform gray pixels produce stable black or white output.
std::string buildPng(const uint16_t w, const uint16_t h, const uint8_t gray, const size_t noiseBytes = 0) {
  std::string raw;
  raw.reserve((static_cast<size_t>(w) * 3 + 1) * h);
  for (uint16_t y = 0; y < h; y++) {
    raw.push_back('\0');
    raw.append(static_cast<size_t>(w) * 3, gray);
  }

  std::string zlib;
  zlib.push_back(0x78);
  zlib.push_back(0x01);
  size_t off = 0;
  while (off < raw.size()) {
    const size_t n = std::min<size_t>(65535, raw.size() - off);
    const bool last = off + n == raw.size();
    zlib.push_back(static_cast<char>(last ? 1 : 0));
    zlib.push_back(static_cast<char>(n & 0xFF));
    zlib.push_back(static_cast<char>((n >> 8) & 0xFF));
    zlib.push_back(static_cast<char>(~n & 0xFF));
    zlib.push_back(static_cast<char>((~n >> 8) & 0xFF));
    zlib.append(raw, off, n);
    off += n;
  }
  zlib += be32(adler32Of(raw));

  std::string png("\x89PNG\r\n\x1a\n", 8);
  std::string ihdr = be32(w) + be32(h);
  ihdr += '\x08';
  ihdr += '\x02';  // RGB
  ihdr += '\0';
  ihdr += '\0';
  ihdr += '\0';
  appendPngChunk(png, "IHDR", ihdr);
  if (noiseBytes > 0) appendPngChunk(png, "npAD", std::string(noiseBytes, '\xA5'));
  appendPngChunk(png, "IDAT", zlib);
  appendPngChunk(png, "IEND", "");
  return png;
}

// Detects as PNG but fails to decode.
std::string corruptPng() {
  return std::string("\x89PNG\r\n\x1a\n", 8) + "corrupt payload, not a real image stream";
}

std::string containerXml() {
  return "<?xml version=\"1.0\"?>"
         "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
         "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
         "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
}

std::string chapterXhtml() {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Chapter</title></head>"
         "<body><p>Hello EPUB</p></body></html>";
}

std::string opfItem(const std::string& id, const std::string& href, const char* mediaType,
                    const std::string& properties = "") {
  return "<item id=\"" + id + "\" href=\"" + href + "\" media-type=\"" + mediaType + "\"" +
         (properties.empty() ? "" : " properties=\"" + properties + "\"") + "/>";
}

std::string buildOpf(const char* version, const std::string& metadataExtra, const std::string& manifestExtra,
                     const std::string& guideXml = "") {
  std::string opf = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
  opf += "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"";
  opf += version;
  opf += "\" unique-identifier=\"id\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">";
  opf += "<dc:title>Cover Book</dc:title><dc:creator>Fixture Author</dc:creator>";
  opf += "<dc:identifier id=\"id\">cover-test-1</dc:identifier>";
  opf += metadataExtra;
  opf += "</metadata><manifest>";
  opf += opfItem("ch1", "text/ch1.xhtml", "application/xhtml+xml");
  opf += manifestExtra;
  opf += "</manifest><spine><itemref idref=\"ch1\"/></spine>";
  opf += guideXml;
  opf += "</package>";
  return opf;
}

std::string buildEpubZip(const std::string& opf, const std::vector<ZipEntry>& extra = {}) {
  std::vector<ZipEntry> entries = {
      {"mimetype", "application/epub+zip"},
      {"META-INF/container.xml", containerXml()},
      {"OEBPS/content.opf", opf},
      {"OEBPS/text/ch1.xhtml", chapterXhtml()},
  };
  entries.insert(entries.end(), extra.begin(), extra.end());
  return buildStoredZip(entries);
}

std::string imgWrapper(const std::string& body, const std::string& doctype = "") {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + doctype +
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Cover</title></head>"
         "<body>" +
         body + "</body></html>";
}

// EPUB3 nav document with a landmarks section; hrefs are relative to the nav
// document directory.
std::string navDocument(const std::string& coverHref) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
         "<head><title>Navigation</title></head><body>"
         "<nav epub:type=\"toc\"><ol><li><a href=\"ch1.xhtml\">Chapter 1</a></li></ol></nav>"
         "<nav epub:type=\"landmarks\"><ol>"
         "<li><a epub:type=\"bodymatter\" href=\"ch1.xhtml\">Start reading</a></li>"
         "<li><a epub:type=\"cover\" href=\"" +
         coverHref +
         "\">Cover</a></li>"
         "</ol></nav></body></html>";
}

std::string sidecarPathFor(const std::string& epubPath) {
  const size_t slash = epubPath.find_last_of('/');
  const size_t dot = epubPath.find_last_of('.');
  return epubPath.substr(0, slash + 1) + epubPath.substr(slash + 1, dot - slash - 1) + ".bmp";
}

int coverPixelAt(const std::string& path, const int x, const int y, bool* ok) {
  *ok = false;
  FsFile file;
  if (!SdMan.openFileForRead("ECT", path, file)) return -1;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return -1;
  }
  std::vector<uint8_t> rowBuffer(bitmap.getRowBytes());
  std::vector<uint8_t> gray(bitmap.getWidth());
  const bool rowOk = bitmap.readGrayscaleRow(gray.data(), gray.size(), rowBuffer.data(), rowBuffer.size(), y) ==
                     BmpReaderError::Ok;
  file.close();
  *ok = rowOk;
  return rowOk ? gray[x] : -1;
}

struct CoverCase {
  const char* name;
  std::string epubPath;
  std::string cacheDir;
  std::string zip;
  std::string sidecar;  // External <stem>.bmp image
  int expectWidth = 0;
  int expectHeight = 0;
  int probeX = -1;  // Pixel position to check in the output
  int probeY = 0;
  int probeMin = 0;
  int probeMax = 255;
};

void runCoverCase(TestUtils::TestRunner& runner, const CoverCase& c) {
  SdMan.clearFiles();
  SdMan.clearWrittenFiles();
  SdMan.reset();
  SdMan.registerFile(c.epubPath, c.zip);
  if (!c.sidecar.empty()) SdMan.registerFile(sidecarPathFor(c.epubPath), c.sidecar);

  const std::string label(c.name);
  Epub epub(c.epubPath, c.cacheDir);
  runner.expectTrue(epub.load(), label + ": loads");
  runner.expectTrue(epub.generateCoverBmp(true), label + ": generates cover");
  home_thumbnail::Info info;
  runner.expectTrue(home_thumbnail::validateCover(epub.getCoverBmpPath(), &info), label + ": cover is valid");
  runner.expectEq(c.expectWidth, info.width, label + ": width");
  runner.expectEq(c.expectHeight, info.height, label + ": height");
  if (c.probeX >= 0) {
    bool ok = false;
    const int pixel = coverPixelAt(epub.getCoverBmpPath(), c.probeX, c.probeY, &ok);
    runner.expectTrue(ok, label + ": pixel readable");
    runner.expectTrue(pixel >= c.probeMin && pixel <= c.probeMax, label + ": pixel from chosen image");
  }
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("EpubCoverDiscoveryTest");
  auto& identity = papyrix::board::HardwareIdentity::instance();
  runner.expectTrue(
      identity.applyBoardSelection({papyrix::board::BoardId::X3, papyrix::board::BoardSelectionSource::Override}, {}),
      "board selected");

  // Use a different image at guessed paths to detect incorrect precedence.
  const auto commonDecoyEntries = std::vector<ZipEntry>{
      {"OEBPS/images/cover.png", buildPng(44, 28, 0)}};

  {
    const std::string opf = buildOpf("3.0", "", opfItem("cvr", "images/prop.png", "image/png", "cover-image"));
    const std::string zip = buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(36, 24, 255)}});
    runCoverCase(runner, {"sidecar_precedence", "/books/sidecar.epub", "/cache01", zip, build24bpp(60, 30, 0), 60,
                          30});
  }

  // ---- cover-image property token variants beat meta target and guessed filename ----
  for (const char* variant : {"cover-image", "cover-image nav", "nav cover-image", "nav cover-image scripted"}) {
    const std::string meta = "<meta name=\"cover\" content=\"metaimg\"/>";
    const std::string manifest = opfItem("cvr", "images/prop.png", "image/png", variant) +
                                 opfItem("metaimg", "images/meta.bmp", "image/bmp");
    const std::string opf = buildOpf("3.0", meta, manifest);
    const std::string zip =
        buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(36, 24, 255)},
                           {"OEBPS/images/meta.bmp", build24bpp(40, 28, 0)},
                           {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    const std::string name = std::string("property_token[") + variant + "]";
    runCoverCase(runner, {name.c_str(), "/books/prop.epub", "/cache02", zip, "", 36, 24, 0, 0, 200, 255});
  }

  {
    const std::string manifest = opfItem("p1", "images/p1.png", "image/png", "cover-image") +
                                 opfItem("p2", "images/p2.png", "image/png", "cover-image");
    const std::string opf = buildOpf("3.0", "", manifest);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/images/p1.png", buildPng(30, 24, 0)},
                                               {"OEBPS/images/p2.png", buildPng(44, 28, 255)}});
    runCoverCase(runner, {"property_first_wins", "/books/propfirst.epub", "/cache03", zip, "", 30, 24, 0, 0, 0, 55});
  }

  {
    const std::string meta = "<meta name=\"cover\" content=\"metaimg\"/>";
    const std::string manifest = opfItem("metaimg", "images/meta.bmp", "image/bmp");
    const std::string opf = buildOpf("2.0", meta, manifest);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/images/meta.bmp", build24bpp(40, 28, 255)},
                                               {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    runCoverCase(runner, {"meta_target", "/books/meta.epub", "/cache04", zip, "", 40, 28, 0, 0, 200, 255});
  }

  {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string opf = buildOpf("2.0", "<meta name=\"cover\" content=\"wrap\"/>", manifest);
    const std::string zip = buildEpubZip(opf, {
        {"OEBPS/text/wrap.xhtml", imgWrapper("<img src='../images/main.png'/>")},
        {"OEBPS/images/main.png", buildPng(40, 28, 255)}});
    runCoverCase(runner, {"metadata_wrapper", "/books/metawrap.epub", "/cache-meta", zip, "", 40, 28});
  }

  {
    const std::string meta = "<meta name=\"cover\" content=\"metaimg\"/>";
    const std::string manifest = opfItem("metaimg", "images/broken.png", "image/png") +
                                 opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string opf = buildOpf("2.0", meta, manifest, guide);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/images/broken.png", corruptPng()},
                                               {"OEBPS/text/wrap.xhtml", imgWrapper("<img src=\"../images/guide.png\"/>")},
                                               {"OEBPS/images/guide.png", buildPng(80, 40, 255)},
                                               {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    runCoverCase(runner, {"meta_corrupt_falls_to_guide", "/books/metacorrupt.epub", "/cache05", zip, "", 80, 40});
  }

  // ---- guide wrapper variants: relative href, single quotes, SVG href/xlink:href ----
  struct GuideVariant {
    const char* name;
    std::string body;
    ZipEntry imageEntry;
    int width;
    int height;
    int probeMin;
    int probeMax;
  };
  const GuideVariant guideVariants[] = {
      {"guide_relative", "<img src=\"../images/rel.png\"/>", {"OEBPS/images/rel.png", buildPng(48, 32, 255)}, 48, 32,
       200, 255},
      {"guide_single_quote", "<img src='../images/single.png'/>", {"OEBPS/images/single.png", buildPng(44, 28, 0)},
       44, 28, 0, 55},
      {"guide_svg_href",
       "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\">"
       "<image href=\"../images/svgh.png\" width=\"100\" height=\"50\"/></svg>",
       {"OEBPS/images/svgh.png", buildPng(46, 30, 255)}, 46, 30, 200, 255},
      {"guide_svg_xlink",
       "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
       "width=\"100\" height=\"50\">"
       "<image xlink:href=\"../images/svgx.png\" width=\"100\" height=\"50\"/></svg>",
       {"OEBPS/images/svgx.png", buildPng(42, 26, 255)}, 42, 26, 200, 255},
  };
  for (const GuideVariant& v : guideVariants) {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string opf = buildOpf("2.0", "", manifest, guide);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", imgWrapper(v.body)}, v.imageEntry});
    runCoverCase(runner, {v.name, "/books/guide.epub", "/cache06", zip, "", v.width, v.height, 0, 0, v.probeMin,
                          v.probeMax});
  }

  {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string body = "<!-- <img src=\"../images/banner.png\"/> -->"
                             "<img src=\"../images/small.png\"/>"
                             "<img src=\"../images/large.png\"/>";
    const std::string opf = buildOpf("2.0", "", manifest, guide);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", imgWrapper(body)},
                                               {"OEBPS/images/banner.png", buildPng(200, 100, 0)},
                                               {"OEBPS/images/small.png", buildPng(30, 24, 0)},
                                               {"OEBPS/images/large.png", buildPng(80, 40, 255)}});
    runCoverCase(runner, {"wrapper_comment_ignored_largest_chosen", "/books/banner.epub", "/cache07", zip, "", 80, 40,
                          0, 0, 200, 255});
  }

  {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string body = "<img src=\"../images/absent.png\"/>"
                             "<img src=\"../images/broken.png\"/>"
                             "<img src=\"../images/good.png\"/>";
    const std::string opf = buildOpf("2.0", "", manifest, guide);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", imgWrapper(body)},
                                               {"OEBPS/images/broken.png", corruptPng()},
                                               {"OEBPS/images/good.png", buildPng(48, 24, 255)}});
    runCoverCase(runner, {"wrapper_missing_then_corrupt_skipped", "/books/skip.epub", "/cache08", zip, "", 48, 24});
  }

  // ---- ranking uses decoded area, not compressed file size ----
  {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string body = "<img src=\"../images/small_heavy.png\"/>"
                             "<img src=\"../images/large_light.png\"/>";
    const std::string opf = buildOpf("2.0", "", manifest, guide);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", imgWrapper(body)},
                                               {"OEBPS/images/small_heavy.png", buildPng(40, 28, 255, 64 * 1024)},
                                               {"OEBPS/images/large_light.png", buildPng(80, 40, 0)}});
    runCoverCase(runner, {"wrapper_area_not_file_size", "/books/area.epub", "/cache09", zip, "", 80, 40, 0, 0, 0, 55});
  }

  // ---- EPUB3 landmarks nav leads to the cover wrapper ----
  {
    const std::string manifest = opfItem("nav", "text/nav.xhtml", "application/xhtml+xml", "nav") +
                                 opfItem("wrap", "text/cover.xhtml", "application/xhtml+xml") +
                                 opfItem("img", "images/landmark.png", "image/png");
    const std::string opf = buildOpf("3.0", "", manifest);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/nav.xhtml", navDocument("cover.xhtml")},
                                               {"OEBPS/text/cover.xhtml", imgWrapper("<img src=\"../images/landmark.png\"/>")},
                                               {"OEBPS/images/landmark.png", buildPng(50, 28, 255)},
                                               {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    runCoverCase(runner, {"landmark_epub3", "/books/landmark.epub", "/cache10", zip, "", 50, 28});
  }

  {
    const std::string manifest = opfItem("nav", "text/nav.xhtml", "application/xhtml+xml", "nav");
    std::string nav = navDocument("#cover");
    nav.insert(nav.find("</body>"), "<img id=\"cover\" src=\"../images/main.png\"/>");
    const std::string zip = buildEpubZip(buildOpf("3.0", "", manifest),
                                        {{"OEBPS/text/nav.xhtml", nav},
                                         {"OEBPS/images/main.png", buildPng(50, 28, 255)}});
    runCoverCase(runner, {"same_document_landmark", "/books/same-page.epub", "/cache-same-page", zip, "", 50, 28});
  }

  for (const bool missing : {false, true}) {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml", "cover-image");
    std::string body;
    for (int i = 0; i < 32; ++i) body += "<img src='../images/logo.png'/>";
    body += "<img src='../images/main.png'/>";
    std::vector<ZipEntry> entries = {{"OEBPS/text/wrap.xhtml", imgWrapper(body)},
                                     {"OEBPS/images/main.png", buildPng(50, 28, 255)}};
    if (!missing) entries.push_back({"OEBPS/images/logo.png", buildPng(20, 20, 0)});
    runCoverCase(runner, {missing ? "repeated_missing_link" : "repeated_logo",
                         "/books/repeated.epub", "/cache-repeated", buildEpubZip(buildOpf("3.0", "", manifest), entries),
                         "", 50, 28});
  }

  {
    SdMan.reset();
    SdMan.registerFile("/low-heap.epub", buildEpubZip(buildOpf("2.0", "", ""),
                                                     {{"cover.bmp", build24bpp(24, 24, 255)}}));
    Epub epub("/low-heap.epub", "/cache-low-heap");
    runner.expectTrue(epub.load(), "low_heap: loads");
    testSetLargestFreeBlock(16 * 1024);
    const bool generated = epub.generateCoverBmp(true);
    testResetLargestFreeBlock();
    runner.expectTrue(generated, "low_heap: small common-path BMP remains available");
    home_thumbnail::Info info;
    runner.expectTrue(home_thumbnail::validateCover(epub.getCoverBmpPath(), &info), "low_heap: valid output");
    runner.expectEq(24, info.width, "low_heap: expected width");
    runner.expectEq(24, info.height, "low_heap: expected height");
  }

  // ---- manifest id fallback (id cover / cover-image) ----
  for (const char* itemId : {"cover", "cover-image"}) {
    const std::string manifest = opfItem(itemId, "images/mid.png", "image/png");
    const std::string opf = buildOpf("2.0", "", manifest);
    const std::string zip =
        buildEpubZip(opf, {{"OEBPS/images/mid.png", buildPng(32, 24, 255)},
                           {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    const std::string name = std::string("manifest_id_") + itemId;
    runCoverCase(runner, {name.c_str(), "/books/manifestid.epub", "/cache11", zip, "", 32, 24});
  }

  // ---- guessed filename is the last resort ----
  {
    const std::string opf = buildOpf("2.0", "", "");
    const std::string zip = buildEpubZip(opf, commonDecoyEntries);
    runCoverCase(runner, {"common_filename_fallback", "/books/common.epub", "/cache12", zip, "", 44, 28});
  }

  // ---- oversized wrapper is skipped ----
  {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string body = "<!--" + std::string(70 * 1024, 'x') + "--><img src=\"../images/inside.png\"/>";
    const std::string opf = buildOpf("2.0", "", manifest, guide);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", imgWrapper(body)},
                                               {"OEBPS/images/inside.png", buildPng(60, 40, 255)},
                                               {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    runCoverCase(runner, {"oversized_wrapper_skipped", "/books/oversize.epub", "/cache13", zip, "", 44, 28});
  }

  // ---- malformed wrapper XML is rejected ----
  {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string truncated = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                                  "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><img src=\"../images/lost.png\"";
    const std::string opf = buildOpf("2.0", "", manifest, guide);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", truncated},
                                               {"OEBPS/images/lost.png", buildPng(60, 40, 255)},
                                               {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    runCoverCase(runner, {"malformed_wrapper_skipped", "/books/malformed.epub", "/cache14", zip, "", 44, 28});
  }

  // ---- malformed landmarks nav is rejected ----
  {
    const std::string manifest = opfItem("nav", "text/nav.xhtml", "application/xhtml+xml", "nav") +
                                 opfItem("wrap", "text/cover.xhtml", "application/xhtml+xml");
    const std::string opf = buildOpf("3.0", "", manifest);
    const std::string truncatedNav = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                                     "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
                                     "xmlns:epub=\"http://www.idpf.org/2007/ops\"><body>"
                                     "<nav epub:type=\"landmarks\"><ol><li><a epub:type=\"cover\" "
                                     "href=\"cover.xhtml\"";
    const std::string zip = buildEpubZip(opf, {{"OEBPS/text/nav.xhtml", truncatedNav},
                                               {"OEBPS/text/cover.xhtml", imgWrapper("<img src=\"../images/lost.png\"/>")},
                                               {"OEBPS/images/lost.png", buildPng(60, 40, 255)},
                                               {"OEBPS/images/cover.png", buildPng(44, 28, 0)}});
    runCoverCase(runner, {"malformed_nav_skipped", "/books/malformednav.epub", "/cache15", zip, "", 44, 28});
  }

  // ---- standard XHTML DOCTYPE is accepted, internal entity subset is not ----
  {
    const std::string manifest = opfItem("wrap", "text/wrap.xhtml", "application/xhtml+xml");
    const std::string guide = "<guide><reference type=\"cover\" href=\"text/wrap.xhtml\"/></guide>";
    const std::string doctypeOk =
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" "
        "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">";
    const std::string doctypeEntity = "<!DOCTYPE html [ <!ENTITY xxe SYSTEM \"file:///etc/passwd\"> ]>";
    const std::string body = "<img src=\"../images/dt.png\"/>";
    const std::string opf = buildOpf("2.0", "", manifest, guide);
    runCoverCase(runner, {"wrapper_external_doctype_ok",
                          "/books/doctype.epub",
                          "/cache16",
                          buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", imgWrapper(body, doctypeOk)},
                                             {"OEBPS/images/dt.png", buildPng(40, 24, 255)}}),
                          "",
                          40,
                          24});
    runCoverCase(runner, {"wrapper_internal_entity_rejected",
                          "/books/doctype.epub",
                          "/cache17",
                          buildEpubZip(opf, {{"OEBPS/text/wrap.xhtml", imgWrapper(body, doctypeEntity)},
                                             {"OEBPS/images/dt.png", buildPng(60, 40, 255)},
                                             {"OEBPS/images/cover.png", buildPng(44, 28, 0)}}),
                          "",
                          44,
                          28});
  }

  // ---- large source is scaled to board cover bounds ----
  {
    const std::string meta = "<meta name=\"cover\" content=\"metaimg\"/>";
    const std::string manifest = opfItem("metaimg", "images/big.bmp", "image/bmp");
    const std::string opf = buildOpf("2.0", meta, manifest);
    const std::string zip = buildEpubZip(opf, {{"OEBPS/images/big.bmp", build24bpp(600, 200, 128)}});
    runCoverCase(runner, {"scaled_to_board_bounds", "/books/scaled.epub", "/cache18", zip, "", 528, 176});
  }

  // ---- transient write failure recovers and leaves no failure marker ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    const std::string opf = buildOpf("3.0", "", opfItem("cvr", "images/prop.png", "image/png", "cover-image"));
    SdMan.registerFile("/books/writefail.epub", buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(40, 24, 255)}}));
    SdMan.registerFile("/books/writefail.bmp", build24bpp(48, 32, 255));

    Epub epub("/books/writefail.epub", "/cache19");
    runner.expectTrue(epub.load(), "write_failure: loads");
    SdMan.setOpenFileForWriteFailCount(1);
    runner.expectFalse(epub.generateCoverBmp(true), "write_failure: generation fails");
    runner.expectTrue(!SdMan.exists(epub.getCachePath() + "/.cover.failed"),
                      "write_failure: no sticky failure marker");
    SdMan.setOpenFileForWriteFailCount(0);
    runner.expectTrue(epub.generateCoverBmp(true), "write_failure: retry succeeds");
    home_thumbnail::Info info;
    runner.expectTrue(home_thumbnail::validateCover(epub.getCoverBmpPath(), &info), "write_failure: cover valid");
    runner.expectEq(48, info.width, "write_failure: width");
    runner.expectEq(32, info.height, "write_failure: height");
  }

  {
    SdMan.reset();
    const std::string opf = buildOpf("3.0", "", opfItem("main", "art/main.png", "image/png", "cover-image"));
    SdMan.registerFile("/sync.epub", buildEpubZip(opf, {{"OEBPS/art/main.png", buildPng(40, 28, 255)}}));
    Epub epub("/sync.epub", "/sync-cache");
    runner.expectTrue(epub.load(), "sync_failure: loads");
    SdMan.setSyncResult(false);
    runner.expectFalse(epub.generateCoverBmp(true), "sync_failure: refuses unsynchronized source");
    runner.expectFalse(SdMan.exists(epub.getCoverBmpPath()), "sync_failure: no published cover");
    SdMan.setSyncResult(true);
    bool abort = false;
    SdMan.setReadObserver([&](const char* path) {
      if (std::string(path).find("/.cover-source.tmp") != std::string::npos) abort = true;
    });
    runner.expectFalse(epub.generateCoverBmp(true, [&] { return abort; }), "cancellation: stops after extraction");
    runner.expectTrue(abort, "cancellation: reaches extracted image");
    runner.expectFalse(SdMan.exists(epub.getCoverBmpPath()), "cancellation: no published cover");
    runner.expectFalse(SdMan.exists(epub.getCachePath() + "/.cover-source.tmp"), "cancellation: source removed");
    SdMan.setReadObserver({});
    runner.expectTrue(epub.generateCoverBmp(true), "sync_failure: subsequent request succeeds");
  }

  // ---- legacy failure markers are cleared on load, valid cover retained ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    const std::string opf = buildOpf("3.0", "", opfItem("cvr", "images/prop.png", "image/png", "cover-image"));
    SdMan.registerFile("/books/legacy.epub", buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(40, 24, 255)}}));

    Epub first("/books/legacy.epub", "/cache20");
    first.setupCacheDir();
    SdMan.registerFile("/seed.bmp", build24bpp(40, 24, 255));
    runner.expectTrue(CoverHelpers::convertImageToBmp("/seed.bmp", first.getCoverBmpPath(), "TEST", true),
                      "marker_migration: old cover seeded");

    SdMan.registerFile(first.getCachePath() + "/.cover.failed", "");
    SdMan.registerFile(first.getCachePath() + "/.thumb.failed", "");

    Epub second("/books/legacy.epub", "/cache20");
    runner.expectTrue(second.load(), "marker_migration: second load");
    runner.expectTrue(!SdMan.exists(second.getCachePath() + "/.cover.failed"),
                      "marker_migration: cover marker cleared");
    runner.expectTrue(!SdMan.exists(second.getCachePath() + "/.thumb.failed"),
                      "marker_migration: thumb marker cleared");
    home_thumbnail::Info info;
    runner.expectTrue(home_thumbnail::validateCover(second.getCoverBmpPath(), &info),
                      "marker_migration: cached cover retained");
    runner.expectEq(40, info.width, "marker_migration: retained width");
  }

  // ---- stale book.bin does not conceal cover routes ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    const std::string bareOpf = buildOpf("2.0", "", "");
    SdMan.registerFile("/books/evolve.epub", buildEpubZip(bareOpf));
    Epub v1("/books/evolve.epub", "/cache21");
    runner.expectTrue(v1.load(), "stale_cache: v1 loads");
    runner.expectFalse(v1.generateCoverBmp(true), "stale_cache: v1 has no cover");

    const std::string opf = buildOpf("3.0", "", opfItem("cvr", "images/prop.png", "image/png", "cover-image"));
    SdMan.registerFile("/books/evolve.epub", buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(40, 24, 255)}}));
    Epub v2("/books/evolve.epub", "/cache21");
    runner.expectTrue(v2.load(), "stale_cache: v2 loads with stale book.bin");
    runner.expectTrue(v2.generateCoverBmp(true), "stale_cache: v2 cover discovered after earlier failure");
    home_thumbnail::Info info;
    runner.expectTrue(home_thumbnail::validateCover(v2.getCoverBmpPath(), &info), "stale_cache: v2 cover valid");
    runner.expectEq(40, info.width, "stale_cache: v2 width");
    runner.expectEq(24, info.height, "stale_cache: v2 height");
  }

  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    const std::string opf = buildOpf("3.0", "", opfItem("cvr", "images/prop.png", "image/png", "cover-image"));
    SdMan.registerFile("/books/cancel.epub", buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(40, 24, 255)}}));

    Epub epub("/books/cancel.epub", "/cache22");
    runner.expectTrue(epub.load(), "cancellation: loads");
    runner.expectFalse(epub.generateCoverBmp(true, [] { return true; }), "cancellation: aborted run fails");
    runner.expectTrue(!SdMan.exists(epub.getCoverBmpPath()), "cancellation: no cover output");
    runner.expectTrue(!SdMan.exists(epub.getCachePath() + "/.cover.failed"), "cancellation: no failure marker");
    runner.expectTrue(epub.generateCoverBmp(true), "cancellation: retry succeeds");
    runner.expectTrue(home_thumbnail::validateCover(epub.getCoverBmpPath()), "cancellation: cover valid");
  }

  {
    SdMan.reset();
    const std::string opf = buildOpf("3.0", "", opfItem("main", "images/main.png", "image/png", "cover-image"));
    SdMan.registerFile("/publish-cancel.epub", buildEpubZip(opf, {{"OEBPS/images/main.png", buildPng(40, 24, 255)}}));
    Epub epub("/publish-cancel.epub", "/cache-publish-cancel");
    runner.expectTrue(epub.load(), "publication_cancel: loads");
    bool published = false;
    const bool generated = epub.generateCoverBmp(true, [&] {
      published |= SdMan.exists(epub.getCoverBmpPath());
      return published;
    });
    runner.expectTrue(published, "publication_cancel: reaches published cover");
    runner.expectFalse(generated, "publication_cancel: request is cancelled");
    runner.expectFalse(SdMan.exists(epub.getCoverBmpPath()), "publication_cancel: published cover removed");
    runner.expectFalse(SdMan.exists(epub.getCachePath() + "/.cover-source.tmp"),
                       "publication_cancel: extracted source removed");
    runner.expectTrue(epub.generateCoverBmp(true), "publication_cancel: subsequent request succeeds");
    runner.expectTrue(home_thumbnail::validateCover(epub.getCoverBmpPath()), "publication_cancel: retry is valid");
  }

  // ---- a valid cached cover is kept without touching the EPUB ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    const std::string opf = buildOpf("3.0", "", opfItem("cvr", "images/prop.png", "image/png", "cover-image"));
    SdMan.registerFile("/books/cached.epub", buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(40, 24, 255)}}));

    Epub epub("/books/cached.epub", "/cache23");
    runner.expectTrue(epub.load(), "cached_cover: loads");
    runner.expectTrue(epub.generateCoverBmp(true), "cached_cover: initial cover generated");
    SdMan.registerFile("/books/cached.epub", "not a zip anymore");
    runner.expectTrue(epub.generateCoverBmp(true), "cached_cover: cached cover reused");
    home_thumbnail::Info info;
    runner.expectTrue(home_thumbnail::validateCover(epub.getCoverBmpPath(), &info), "cached_cover: still valid");
    runner.expectEq(40, info.width, "cached_cover: width retained");
    runner.expectEq(24, info.height, "cached_cover: height retained");
  }

  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.reset();
    const std::string bareOpf = buildOpf("2.0", "", "");
    SdMan.registerFile("/books/thumb.epub", buildEpubZip(bareOpf));

    papyrix::ContentHandle noCover;
    runner.expectTrue(noCover.open("/books/thumb.epub", "/cache24").ok(), "thumbnail: opens coverless EPUB");
    runner.expectTrue(noCover.generateThumbnail(nullptr) == home_thumbnail::Result::Unavailable,
                      "thumbnail: coverless EPUB is Unavailable");
    const std::string thumbPath = noCover.getThumbnailPath();
    const std::string thumbMarker = thumbPath.substr(0, thumbPath.find_last_of('/')) + "/.thumb.failed";
    runner.expectTrue(!SdMan.exists(thumbMarker), "thumbnail: no sticky marker after missing cover");

    SdMan.registerFile(thumbMarker, "");  // legacy marker from an older failure
    const std::string opf = buildOpf("3.0", "", opfItem("cvr", "images/prop.png", "image/png", "cover-image"));
    SdMan.registerFile("/books/thumb.epub", buildEpubZip(opf, {{"OEBPS/images/prop.png", buildPng(40, 24, 255)}}));
    papyrix::ContentHandle recovered;
    runner.expectTrue(recovered.open("/books/thumb.epub", "/cache24").ok(), "thumbnail: opens updated EPUB");
    runner.expectTrue(!recovered.generateCover(true, nullptr).empty(), "thumbnail: cover generated");
    runner.expectTrue(!SdMan.exists(thumbMarker), "thumbnail: successful cover clears legacy marker");
    runner.expectTrue(recovered.generateThumbnail(nullptr) == home_thumbnail::Result::Ready,
                      "thumbnail: ready after recovery");
    runner.expectTrue(home_thumbnail::validate(recovered.getThumbnailPath()), "thumbnail: output valid");
  }

  return runner.allPassed() ? 0 : 1;
}
