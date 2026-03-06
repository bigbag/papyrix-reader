#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <EInkDisplay.h>
#include <EpdFont.h>
#include <ExternalFont.h>
#include <Epub.h>
#include <EpubChapterParser.h>
#include <Fb2.h>
#include <Fb2Parser.h>
#include <Page.h>
#include <GfxRenderer.h>
#include <Markdown.h>
#include <MarkdownParser.h>
#include <PageCache.h>
#include <PlainTextParser.h>
#include <RenderConfig.h>
#include <SDCardManager.h>
#include <Txt.h>
#include <Utf8.h>
#include <FsHelpers.h>
#include <LittleFS.h>

#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/reader_italic_2b.h>

// LittleFS global
MockLittleFS LittleFS;

// GfxRenderer static frame buffer
uint8_t GfxRenderer::frameBuffer_[EInkDisplay::BUFFER_SIZE];

enum ContentType { EPUB, MARKDOWN, TXT_FILE, FB2_FILE, UNKNOWN };

static ContentType detectType(const std::string& path) {
  if (FsHelpers::hasExtension(path, ".epub")) return EPUB;
  if (FsHelpers::hasExtension(path, ".md") || FsHelpers::hasExtension(path, ".markdown")) return MARKDOWN;
  if (FsHelpers::hasExtension(path, ".txt")) return TXT_FILE;
  if (FsHelpers::hasExtension(path, ".fb2")) return FB2_FILE;
  return UNKNOWN;
}

static bool mkdirRecursive(const std::string& path) {
  std::string current;
  for (size_t i = 0; i < path.size(); i++) {
    current += path[i];
    if (path[i] == '/' && i > 0) {
      mkdir(current.c_str(), 0755);
    }
  }
  mkdir(path.c_str(), 0755);
  return true;
}

static std::string renderWord(const GfxRenderer& gfx, int fontId, const std::string& word) {
  std::string result;
  const char* ptr = word.c_str();
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr)))) {
    if (gfx.hasGlyph(fontId, cp)) {
      // Encode codepoint back to UTF-8
      if (cp < 0x80) {
        result += static_cast<char>(cp);
      } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
      }
    } else {
      result += '?';
    }
  }
  return result;
}

static void dumpPages(PageCache& cache, const GfxRenderer& gfx, int fontId) {
  for (int p = 0; p < cache.pageCount(); p++) {
    auto page = cache.loadPage(p);
    if (!page) continue;
    printf("    --- Page %d ---\n", p);
    for (auto& elem : page->elements) {
      if (elem->getTag() == TAG_PageLine) {
        auto& tb = static_cast<PageLine*>(elem.get())->getTextBlock();
        for (auto& wd : tb.getWords()) {
          printf("%s ", renderWord(gfx, fontId, wd.word).c_str());
        }
        printf("\n");
      }
    }
  }
}

static void dumpCacheDir(const std::string& dir, const GfxRenderer& gfx, int fontId) {
  // Find and dump .bin files in sections/ subdirectory
  std::string sectionsDir = dir + "/sections";
  struct stat st;
  const std::string& scanDir = (stat(sectionsDir.c_str(), &st) == 0) ? sectionsDir : dir;

  // Collect and sort .bin files
  std::vector<std::string> binFiles;
  DIR* d = opendir(scanDir.c_str());
  if (!d) {
    fprintf(stderr, "Cannot open directory: %s\n", scanDir.c_str());
    return;
  }
  struct dirent* entry;
  while ((entry = readdir(d)) != nullptr) {
    std::string name = entry->d_name;
    if (FsHelpers::hasExtension(name, ".bin")) {
      binFiles.push_back(scanDir + "/" + name);
    }
  }
  closedir(d);
  std::sort(binFiles.begin(), binFiles.end());

  int totalPages = 0;
  for (auto& path : binFiles) {
    PageCache cache(path);
    if (!cache.loadRaw()) {
      fprintf(stderr, "  Failed to load: %s\n", path.c_str());
      continue;
    }
    fprintf(stderr, "  %s: %d pages%s\n", path.c_str(), cache.pageCount(), cache.isPartial() ? " (partial)" : "");
    dumpPages(cache, gfx, fontId);
    totalPages += cache.pageCount();
  }
  fprintf(stderr, "Total: %d pages\n", totalPages);
}

static void usage() {
  fprintf(stderr, "Usage: reader-test [--dump] [--batch N] [--no-statusbar] [--cjk-font PATH] <file.epub|.md|.txt|.fb2> [output_dir]\n");
  fprintf(stderr, "       reader-test --cache-dump <cache_dir>\n");
  fprintf(stderr, "  --dump           Print parsed text content of each page\n");
  fprintf(stderr, "  --batch N        Cache N pages per batch (default: 5, matching device)\n");
  fprintf(stderr, "                   Use 0 for unlimited (no suspend/resume)\n");
  fprintf(stderr, "  --no-statusbar   Use full viewport height (no status bar margin)\n");
  fprintf(stderr, "  --cjk-font PATH  Load external CJK font (.bin) for text width measurement\n");
  fprintf(stderr, "  --cache-dump     Dump text from existing device cache directory\n");
  fprintf(stderr, "  output_dir defaults to /tmp/papyrix-cache/\n");
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    usage();
    return 1;
  }

  bool dump = false;
  bool showStatusBar = true;
  uint16_t batchSize = 5;
  std::string cjkFontPath;
  int argIdx = 1;
  while (argIdx < argc && argv[argIdx][0] == '-') {
    if (strcmp(argv[argIdx], "--dump") == 0) {
      dump = true;
      argIdx++;
    } else if (strcmp(argv[argIdx], "--no-statusbar") == 0) {
      showStatusBar = false;
      argIdx++;
    } else if (strcmp(argv[argIdx], "--batch") == 0 && argIdx + 1 < argc) {
      batchSize = static_cast<uint16_t>(atoi(argv[argIdx + 1]));
      argIdx += 2;
    } else if (strcmp(argv[argIdx], "--cjk-font") == 0 && argIdx + 1 < argc) {
      cjkFontPath = argv[argIdx + 1];
      argIdx += 2;
    } else if (strcmp(argv[argIdx], "--cache-dump") == 0) {
      // Handled after renderer setup
      break;
    } else {
      break;
    }
  }

  // Setup renderer with real font metrics
  EInkDisplay display(0, 0, 0, 0, 0, 0);
  GfxRenderer gfx(display);
  gfx.begin();

  EpdFont readerFont(&reader_2b);
  EpdFont readerBoldFont(&reader_bold_2b);
  EpdFont readerItalicFont(&reader_italic_2b);
  EpdFontFamily readerFontFamily(&readerFont, &readerBoldFont, &readerItalicFont, &readerBoldFont);
  constexpr int FONT_ID = 1818981670;
  gfx.insertFont(FONT_ID, readerFontFamily);  // READER_FONT_ID

  ExternalFont externalFont;
  if (!cjkFontPath.empty()) {
    if (externalFont.load(cjkFontPath.c_str())) {
      gfx.setExternalFont(&externalFont);
      fprintf(stderr, "CJK font: %s (%dx%d, %d bytes/char)\n",
              externalFont.getFontName(), externalFont.getCharWidth(),
              externalFont.getCharHeight(), externalFont.getBytesPerChar());
    } else {
      fprintf(stderr, "Failed to load CJK font: %s\n", cjkFontPath.c_str());
      return 1;
    }
  }

  // Handle --cache-dump (needs renderer for glyph checking)
  if (argIdx < argc && strcmp(argv[argIdx], "--cache-dump") == 0) {
    if (argIdx + 1 >= argc) {
      usage();
      return 1;
    }
    dumpCacheDir(argv[argIdx + 1], gfx, FONT_ID);
    return 0;
  }

  if (argIdx >= argc) {
    usage();
    return 1;
  }

  const std::string filepath = argv[argIdx];
  const std::string outputDir = argIdx + 1 < argc ? argv[argIdx + 1] : "/tmp/papyrix-cache";

  ContentType type = detectType(filepath);
  if (type == UNKNOWN) {
    fprintf(stderr, "Unsupported file type: %s\n", filepath.c_str());
    return 1;
  }

  RenderConfig config;
  config.fontId = FONT_ID;
  config.viewportWidth = 464;                        // 480 - 2*(3+5)
  config.viewportHeight = showStatusBar ? 765 : 788;  // 800 - 9 - (3+23) or 800 - 9 - 3
  config.paragraphAlignment = 0;
  config.spacingLevel = 1;
  config.lineCompression = 1.0f;
  config.hyphenation = true;  // Enable hyphenation (matches default device setting)

  mkdirRecursive(outputDir);

  if (type == EPUB) {
    auto epub = std::make_shared<Epub>(filepath, outputDir);
    if (!epub->load()) {
      fprintf(stderr, "Failed to load EPUB: %s\n", filepath.c_str());
      return 1;
    }
    printf("EPUB: \"%s\" by %s, %d spine items\n", epub->getTitle().c_str(), epub->getAuthor().c_str(),
           epub->getSpineItemsCount());

    std::string sectionsDir = epub->getCachePath() + "/sections";
    mkdirRecursive(sectionsDir);
    std::string imageCachePath = epub->getCachePath() + "/images";

    int totalPages = 0;
    for (int i = 0; i < epub->getSpineItemsCount(); i++) {
      std::string cachePath = sectionsDir + "/" + std::to_string(i) + ".bin";
      EpubChapterParser parser(epub, i, gfx, config, imageCachePath);
      PageCache cache(cachePath);
      cache.create(parser, config, batchSize);
      // Extend in batches until all pages are cached, matching device behavior
      while (batchSize > 0 && cache.isPartial()) {
        cache.extend(parser, batchSize);
      }
      printf("  Spine %d: %d pages -> %s\n", i, cache.pageCount(), cachePath.c_str());
      if (dump) dumpPages(cache, gfx, FONT_ID);
      totalPages += cache.pageCount();
    }
    printf("Total: %d pages\n", totalPages);

  } else if (type == MARKDOWN) {
    Markdown md(filepath, outputDir);
    if (!md.load()) {
      fprintf(stderr, "Failed to load Markdown: %s\n", filepath.c_str());
      return 1;
    }
    printf("Markdown: \"%s\"\n", md.getTitle().c_str());

    MarkdownParser parser(filepath, gfx, config);
    std::string cachePath = outputDir + "/pages_0.bin";
    PageCache cache(cachePath);
    cache.create(parser, config, 0);
    printf("Markdown: %d pages -> %s\n", cache.pageCount(), cachePath.c_str());
    if (dump) dumpPages(cache, gfx, FONT_ID);

  } else if (type == FB2_FILE) {
    Fb2 fb2file(filepath, outputDir);
    if (!fb2file.load()) {
      fprintf(stderr, "Failed to load FB2: %s\n", filepath.c_str());
      return 1;
    }
    fb2file.setupCacheDir();
    printf("FB2: \"%s\" by %s (%d TOC entries)\n", fb2file.getTitle().c_str(), fb2file.getAuthor().c_str(),
           fb2file.tocCount());

    Fb2Parser parser(filepath, gfx, config);
    std::string cachePath = outputDir + "/pages_0.bin";
    PageCache cache(cachePath);
    cache.create(parser, config, batchSize);
    while (batchSize > 0 && cache.isPartial()) {
      cache.extend(parser, batchSize);
    }
    printf("FB2: %d pages -> %s\n", cache.pageCount(), cachePath.c_str());
    if (dump) dumpPages(cache, gfx, FONT_ID);

  } else {
    Txt txt(filepath, outputDir);
    if (!txt.load()) {
      fprintf(stderr, "Failed to load TXT: %s\n", filepath.c_str());
      return 1;
    }
    printf("TXT: \"%s\"\n", txt.getTitle().c_str());

    PlainTextParser parser(filepath, gfx, config);
    std::string cachePath = outputDir + "/pages_0.bin";
    PageCache cache(cachePath);
    cache.create(parser, config, 0);
    printf("TXT: %d pages -> %s\n", cache.pageCount(), cachePath.c_str());
    if (dump) dumpPages(cache, gfx, FONT_ID);
  }

  return 0;
}
