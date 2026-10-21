// Chapter anchor placement tests: a navigation anchor must resolve to the
// page that receives its first content, including across page breaks.

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <Page.h>
#include <SDCardManager.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "test_utils.h"

uint8_t GfxRenderer::frameBuffer_[papyrix::hal::Display::BUFFER_SIZE];

namespace {
using Pages = std::vector<std::unique_ptr<Page>>;
using AnchorMap = std::vector<std::pair<std::string, uint32_t>>;
constexpr const char* CHAPTER = "/book/chapter.xhtml";
constexpr const char* CACHE = "/cache/images";
const auto glyphs = [] {
  std::array<EpdGlyph, 95> values{};
  for (auto& glyph : values) {
    glyph.width = 6;
    glyph.height = 10;
    glyph.advanceX = 7;
    glyph.top = 10;
  }
  return values;
}();
const EpdUnicodeInterval intervals[] = {{32, 126, 0}};
const EpdFontData fontData = {nullptr, glyphs.data(), intervals, 1, 14, 10, 3, false};

void cacheImage(const std::string& source, unsigned height) {
  constexpr unsigned width = 20;
  const unsigned pixels = ((width * 3 + 3) / 4) * 4 * height;
  std::string bmp(54 + pixels, '\0');
  const auto put = [&bmp](size_t offset, unsigned value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) bmp[offset + i] = static_cast<char>(value >> (8 * i));
  };
  bmp[0] = 'B';
  bmp[1] = 'M';
  put(2, bmp.size(), 4);
  put(10, 54, 4);
  put(14, 40, 4);
  put(18, width, 4);
  put(22, height, 4);
  put(26, 1, 2);
  put(28, 24, 2);
  put(34, pixels, 4);
  const auto path = FsHelpers::normalisePath("/book/" + source);
  SdMan.registerFile(std::string(CACHE) + "/" + std::to_string(std::hash<std::string>{}(path)) + ".bmp", bmp);
}

std::string filler(unsigned count) {
  std::string result;
  for (unsigned i = 0; i < count; ++i) {
    result += "<p>aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp</p>";
  }
  return result;
}

bool parseChapter(GfxRenderer& renderer, const RenderConfig& config, const std::string& body, unsigned batch,
                  Pages& pages, AnchorMap& anchors) {
  SdMan.registerFile(CHAPTER, "<html><body>" + body + "</body></html>");
  unsigned created = 0;
  bool full = false;
  ChapterHtmlSlimParser parser(
      CHAPTER, renderer, config,
      [&](std::unique_ptr<Page> page) {
        if (full) return false;
        pages.push_back(std::move(page));
        full = batch != 0 && ++created >= batch;
        return !full;
      },
      nullptr, "/book/", CACHE, [](const std::string&, Print&, size_t, BuildArena*) { return false; });
  if (!parser.parseAndBuildPages()) return false;
  for (unsigned guard = 0; parser.isSuspended() && guard < 128; ++guard) {
    created = 0;
    full = false;
    if (!parser.resumeParsing()) return false;
  }
  anchors = parser.getAnchorMap();
  return !parser.isSuspended() && !parser.wasAborted();
}

std::string paraWords(const unsigned count) {
  static const char* pool[] = {"para", "zz", "yy", "xx", "ww", "vv", "uu", "tt", "ss", "rr", "qq", "pp"};
  std::string result;
  for (unsigned i = 0; i < count; ++i) {
    result += pool[i % 12];
    result += ' ';
  }
  return result;
}

long pageOfWord(const Pages& pages, const std::string& word) {
  for (size_t index = 0; index < pages.size(); ++index) {
    for (const auto& element : pages[index]->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      for (const auto& entry : static_cast<const PageLine&>(*element).getTextBlock().getWords()) {
        if (entry.word == word) return static_cast<long>(index);
      }
    }
  }
  return -1;
}

long pageOfImage(const Pages& pages) {
  for (size_t index = 0; index < pages.size(); ++index) {
    for (const auto& element : pages[index]->elements) {
      if (element->getTag() == TAG_PageImage) return static_cast<long>(index);
    }
  }
  return -1;
}

long anchorPage(const AnchorMap& anchors, const std::string& id) {
  for (const auto& entry : anchors) {
    if (entry.first == id) return static_cast<long>(entry.second);
  }
  return -1;
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("ChapterAnchorPlacement");
  SdMan.reset();
  papyrix::hal::Display display(0, 0, 0, 0, 0, 0);
  GfxRenderer renderer(display);
  EpdFont font(&fontData);
  renderer.insertFont(1, EpdFontFamily(&font, &font, &font, &font));
  const RenderConfig config(1, 1.0f, 0, 1, 0, false, true, 300, 120);
  cacheImage("tall.png", 90);
  cacheImage("small.png", 40);
  cacheImage("tiny.png", 6);

  struct Case {
    const char* name;
    std::function<std::string(unsigned)> build;
    std::function<long(const Pages&)> target;
    std::vector<std::string> ids;
  };
  const Case cases[] = {
      {"block",
       [](unsigned n) {
         return filler(n) + "<p id=\"bk\">BKTARGET zz yy xx ww vv uu tt ss rr qq</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "BKTARGET"); },
       {"bk"}},
      {"inline",
       [](unsigned n) {
         return filler(n) + "<p>lead <span id=\"inl\">ILTARGET zzz yyy xxx www vvv uuu ttt sss</span></p>" +
                filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "ILTARGET"); },
       {"inl"}},
      {"mid-word",
       [](unsigned n) {
         return filler(n) + "<p>lead wor<a id=\"mid2\"></a>dings pre<span id=\"mid\">MIDWORD zz yy xx ww vv uu</span></p>" +
                filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "wordings"); },
       {"mid2", "mid"}},
      {"late-inline",
       [](unsigned n) {
         return "<p>" + paraWords(90 + 5 * n) + "<span id=\"late\">LATEGOAL zz yy xx ww vv uu tt ss</span></p>" +
                filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "LATEGOAL"); },
       {"late"}},
      {"empty before image",
       [](unsigned n) {
         return filler(n) + "<p>lead zz yy xx ww vv uu tt ss rr <a id=\"eaim\"></a></p>" +
                "<div><img src=\"tall.png\"/></div><p>afterword</p>";
       },
       [](const Pages& pages) { return pageOfImage(pages); },
       {"eaim"}},
      {"repeated word",
       [](unsigned n) {
         std::string prefix;
         for (unsigned i = 0; i < 40 + 2 * n; ++i) prefix += "REP zz ";
         return "<p>" + prefix + "<span id=\"rep\">REP FINALREP zz yy xx ww</span></p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "FINALREP"); },
       {"rep"}},
      {"empty",
       [](unsigned n) {
         return filler(n) + "<div><a id=\"ea\"></a></div><p>EATARGET zz yy xx ww vv uu tt ss rr</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "EATARGET"); },
       {"ea"}},
      {"nested",
       [](unsigned n) {
         return filler(n) + "<div id=\"outer\"><p id=\"inner\">NETARGET zz yy xx ww vv uu tt ss</p></div>" +
                filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "NETARGET"); },
       {"outer", "inner"}},
      {"hidden",
       [](unsigned n) {
         return filler(n) + "<a id=\"hid\"></a><span hidden=\"\">gapword</span><p>HDTARGET zz yy xx ww vv uu</p>" +
                filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "HDTARGET"); },
       {"hid"}},
      {"tall image",
       [](unsigned n) {
         return filler(n) + "<div id=\"fig\"><img src=\"tall.png\"/></div><p>afterword</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfImage(pages); },
       {"fig"}},
      {"small image",
       [](unsigned n) {
         return filler(n) + "<div id=\"fig2\"><img src=\"small.png\"/></div><p>afterword</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfImage(pages); },
       {"fig2"}},
      {"direct tall image id",
       [](unsigned n) {
         return filler(n) + "<p>tail <img id=\"direct\" src=\"tall.png\"/></p><p>afterword</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfImage(pages); },
       {"direct"}},
      {"direct small image id",
       [](unsigned n) {
         return filler(n) + "<p>tail <img id=\"direct2\" src=\"small.png\"/></p><p>afterword</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfImage(pages); },
       {"direct2"}},
      {"skipped image id",
       [](unsigned n) {
         return filler(n) + "<p>tail <img id=\"unsup\" src=\"bad.gif\"/></p><p>UNSUPAFTER zz yy xx ww</p>" +
                filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "UNSUPAFTER"); },
       {"unsup"}},
      {"table id",
       [](unsigned n) {
         return filler(n) + "<div>tail <table id=\"tab\"><tr><td>CELL</td></tr></table></div>" +
                "<p>TABAFTER zz yy xx ww</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "[Table omitted]"); },
       {"tab"}},
      {"fused tall image id",
       [](unsigned n) {
         return filler(n) + "<p>tail<img id=\"dfuse\" src=\"tall.png\"/></p><p>afterword</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfImage(pages); },
       {"dfuse"}},
      {"fused placeholder id",
       [](unsigned n) {
         return filler(n) + "<p>tail<img id=\"pfuse\" src=\"missing.png\" alt=\"gone\"/></p><p>afterword</p>" +
                filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "[Image: gone]"); },
       {"pfuse"}},
      {"fused table id",
       [](unsigned n) {
         return filler(n) + "<div>tail<table id=\"tfuse\"><tr><td>C</td></tr></table></div>" +
                "<p>TABAFTER zz yy xx</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "[Table omitted]"); },
       {"tfuse"}},
      {"fused skipped image id",
       [](unsigned n) {
         return filler(n) + "<p>tail<img id=\"sfuse\" src=\"bad.gif\"/>suffix</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "tailsuffix"); },
       {"sfuse"}},
      {"fused tiny image id",
       [](unsigned n) {
         return filler(n) + "<p>tail<img id=\"tny\" src=\"tiny.png\"/>suffix</p>" + filler(2);
       },
       [](const Pages& pages) { return pageOfWord(pages, "tailsuffix"); },
       {"tny"}},
  };

  for (const auto& item : cases) {
    Pages reference;
    AnchorMap referenceAnchors;
    runner.expectTrue(
        parseChapter(renderer, config, item.build(12), 0, reference, referenceAnchors),
        std::string(item.name) + " parses without a batch limit");
    runner.expectTrue(reference.size() > 1, std::string(item.name) + " spans several pages");
    for (unsigned n = 0; n <= 12; ++n) {
      const std::string base = std::string(item.name) + " boundary " + std::to_string(n);
      Pages pages;
      AnchorMap anchors;
      runner.expectTrue(parseChapter(renderer, config, item.build(n), 0, pages, anchors), base + " parses");
      const long target = item.target(pages);
      runner.expectTrue(target >= 0, base + " places the target content");
      for (const auto& id : item.ids) {
        runner.expectEq(target, anchorPage(anchors, id),
                        base + " anchor " + id + " resolves to the page that receives the target");
      }
      for (unsigned batch : {1U, 5U}) {
        Pages batched;
        AnchorMap batchedAnchors;
        const std::string label = base + " batch " + std::to_string(batch);
        runner.expectTrue(parseChapter(renderer, config, item.build(n), batch, batched, batchedAnchors),
                          label + " resumes to completion");
        runner.expectTrue(batchedAnchors == anchors, label + " records the same anchor pages");
        for (const auto& id : item.ids) {
          runner.expectEq(target, anchorPage(batchedAnchors, id),
                          label + " anchor " + id + " resolves to the page that receives the target");
        }
      }
    }
  }


  // More contentless ids than the pending cap: rows keep source order and
  // the ids after the cap still bind to the target word.
  {
    const std::string ids = [] {
      std::string result;
      for (unsigned i = 0; i < 40; ++i) result += "<a id=\"cap" + std::to_string(i) + "\"></a>";
      return result;
    }();
    for (unsigned batch : {0U, 5U}) {
      Pages pages;
      AnchorMap anchors;
      runner.expectTrue(parseChapter(renderer, config,
                                     filler(4) + "<p>" + ids + "CAPTARGET zz yy xx ww vv uu</p>" + filler(2), batch,
                                     pages, anchors),
                        "cap overflow batch " + std::to_string(batch) + " parses");
      const long target = pageOfWord(pages, "CAPTARGET");
      runner.expectTrue(target >= 0, "cap overflow batch " + std::to_string(batch) + " places the target");
      size_t searchFrom = 0;
      uint32_t previousPage = 0;
      bool ordered = true;
      for (unsigned i = 0; ordered && i < 40; ++i) {
        const std::string id = "cap" + std::to_string(i);
        size_t at = anchors.size();
        for (size_t k = searchFrom; k < anchors.size(); ++k) {
          if (anchors[k].first == id) {
            at = k;
            break;
          }
        }
        if (at == anchors.size() || anchors[at].second < previousPage ||
            anchors[at].second > static_cast<uint32_t>(target)) {
          ordered = false;
        } else {
          searchFrom = at + 1;
          previousPage = anchors[at].second;
        }
      }
      runner.expectTrue(ordered,
                        "cap overflow batch " + std::to_string(batch) + " keeps source order and bounded pages");
      const auto last = std::find_if(anchors.begin(), anchors.end(),
                                     [](const auto& entry) { return entry.first == "cap39"; });
      runner.expectTrue(last != anchors.end() && static_cast<long>(last->second) == target,
                        "cap overflow batch " + std::to_string(batch) + " binds the last id to the target page");
    }
  }

  // A cancelled parse keeps only anchors whose content reached a finished page.
  {
    Pages pages;
    bool abort = false;
    SdMan.registerFile(CHAPTER, "<html><body>" + filler(2) + "<p id=\"c1\">C1TARGET zz yy</p>" + filler(8) +
                                     "</body></html>");
    ChapterHtmlSlimParser parser(
        CHAPTER, renderer, config,
        [&](std::unique_ptr<Page> page) {
          pages.push_back(std::move(page));
          abort = true;
          return true;
        },
        nullptr, "/book/", CACHE, [](const std::string&, Print&, size_t, BuildArena*) { return false; }, nullptr,
        [&] { return abort; });
    runner.expectTrue(parser.parseAndBuildPages() && parser.wasAborted(), "cancellation stops the parse");
    bool bounded = true;
    for (const auto& entry : parser.getAnchorMap()) {
      if (entry.second >= pages.size()) bounded = false;
    }
    runner.expectTrue(bounded, "a cancelled parse leaves no anchor past the last finished page");
  }

  return runner.allPassed() ? 0 : 1;
}
