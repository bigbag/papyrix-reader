// Production Epub::load() and BookMetadataCache TOC recovery checks. Uses real
// stored-entry EPUB archives on the mock SD card.
#include <Epub.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "test_utils.h"

namespace {

void putLe16(std::string& out, const uint16_t value) {
  out.push_back(value & 0xFF);
  out.push_back((value >> 8) & 0xFF);
}

void putLe32(std::string& out, const uint32_t value) {
  for (int i = 0; i < 4; i++) out.push_back((value >> (8 * i)) & 0xFF);
}

// Minimal multi-entry ZIP with stored (uncompressed) entries. Fixture data only;
// the production ZipFile reader does all real parsing.
std::string buildStoredZip(const std::vector<std::pair<std::string, std::string>>& entries) {
  struct Central {
    std::string name;
    uint32_t offset;
    uint32_t size;
  };

  std::string out;
  std::vector<Central> centrals;
  for (const auto& entry : entries) {
    const auto offset = static_cast<uint32_t>(out.size());
    const auto size = static_cast<uint32_t>(entry.second.size());
    putLe32(out, 0x04034b50);
    putLe16(out, 20);
    putLe16(out, 0);
    putLe16(out, 0);  // stored
    putLe16(out, 0);
    putLe16(out, 0);
    putLe32(out, 0);
    putLe32(out, size);
    putLe32(out, size);
    putLe16(out, static_cast<uint16_t>(entry.first.size()));
    putLe16(out, 0);
    out += entry.first;
    out += entry.second;
    centrals.push_back({entry.first, offset, size});
  }

  const auto cdOffset = static_cast<uint32_t>(out.size());
  for (const auto& central : centrals) {
    putLe32(out, 0x02014b50);
    putLe16(out, 20);
    putLe16(out, 20);
    putLe16(out, 0);
    putLe16(out, 0);
    putLe16(out, 0);
    putLe16(out, 0);
    putLe32(out, 0);
    putLe32(out, central.size);
    putLe32(out, central.size);
    putLe16(out, static_cast<uint16_t>(central.name.size()));
    putLe16(out, 0);
    putLe16(out, 0);
    putLe16(out, 0);
    putLe16(out, 0);
    putLe32(out, 0);
    putLe32(out, central.offset);
    out += central.name;
  }
  const auto cdSize = static_cast<uint32_t>(out.size() - cdOffset);

  putLe32(out, 0x06054b50);
  putLe16(out, 0);
  putLe16(out, 0);
  putLe16(out, static_cast<uint16_t>(centrals.size()));
  putLe16(out, static_cast<uint16_t>(centrals.size()));
  putLe32(out, cdSize);
  putLe32(out, cdOffset);
  putLe16(out, 0);
  return out;
}

std::string containerXml() {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
         "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
         "media-type=\"application/oebps-package+xml\"/></rootfiles>\n"
         "</container>\n";
}

std::string chapterHtml(const int index) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Chapter " +
         std::to_string(index) +
         "</title></head><body><p>Body of chapter " + std::to_string(index) + "</p></body></html>\n";
}

struct EpubSpec {
  std::string title;
  int chapters = 4;
  bool withNav = true;
  bool withNcx = true;
  std::vector<std::string> navTocHrefs;  // hrefs inside nav epub:type="toc"
  std::vector<std::string> navPageListHrefs;
  std::vector<std::string> ncxSrcs;
  std::string navOverride;  // replaces the nav document when non-empty
  std::string ncxOverride;
  // (href, content) in manifest order; entries are stored under OEBPS/.
  std::vector<std::pair<std::string, std::string>> cssFiles;
  // Declared in the manifest but with no stored entry.
  std::vector<std::string> missingCssFiles;
};

std::string navDocument(const EpubSpec& spec) {
  std::string tocList;
  for (const auto& href : spec.navTocHrefs) {
    tocList += "<li><a href=\"" + href + "\">Nav " + href + "</a></li>";
  }
  std::string pageList;
  for (const auto& href : spec.navPageListHrefs) {
    pageList += "<li><a href=\"" + href + "\">" + href + "</a></li>";
  }
  std::string doc = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
                    "xmlns:epub=\"http://www.idpf.org/2007/ops\">\n"
                    "<body>\n";
  if (!tocList.empty()) {
    doc += "<nav epub:type=\"toc\"><ol>" + tocList + "</ol></nav>\n";
  }
  if (!pageList.empty()) {
    doc += "<nav epub:type=\"page-list\"><ol>" + pageList + "</ol></nav>\n";
  }
  doc += "</body>\n</html>\n";
  return doc;
}

std::string ncxDocument(const EpubSpec& spec) {
  std::string points;
  int order = 1;
  for (const auto& src : spec.ncxSrcs) {
    points += "<navPoint id=\"n" + std::to_string(order) + "\" playOrder=\"" + std::to_string(order) +
              "\"><navLabel><text>NCX " + src + "</text></navLabel><content src=\"" + src + "\"/></navPoint>";
    order++;
  }
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
         "<navMap>" +
         points +
         "</navMap>\n"
         "</ncx>\n";
}

std::string makeEpub(const EpubSpec& spec) {
  std::string manifest;
  std::string spine;
  if (spec.withNav) {
    manifest += "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>";
  }
  if (spec.withNcx) {
    manifest += "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>";
  }
  for (const auto& href : spec.missingCssFiles) {
    manifest += "<item id=\"cssmissing\" href=\"" + href + "\" media-type=\"text/css\"/>";
  }
  int cssIndex = 1;
  for (const auto& css : spec.cssFiles) {
    manifest += "<item id=\"css" + std::to_string(cssIndex++) + "\" href=\"" + css.first +
                "\" media-type=\"text/css\"/>";
  }
  for (int i = 1; i <= spec.chapters; i++) {
    manifest += "<item id=\"ch" + std::to_string(i) + "\" href=\"ch" + std::to_string(i) +
                ".xhtml\" media-type=\"application/xhtml+xml\"/>";
    spine += "<itemref idref=\"ch" + std::to_string(i) + "\"/>";
  }

  const std::string opf = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<package xmlns=\"http://www.idpf.org/2007/opf\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                          "version=\"3.0\" unique-identifier=\"id\">\n"
                          "<metadata><dc:title>" +
                          spec.title +
                          "</dc:title><dc:creator>Fixture Author</dc:creator>"
                          "<dc:language>en</dc:language></metadata>\n"
                          "<manifest>" +
                          manifest +
                          "</manifest>\n"
                          "<spine>" +
                          spine +
                          "</spine>\n"
                          "</package>\n";

  std::vector<std::pair<std::string, std::string>> entries = {{"META-INF/container.xml", containerXml()},
                                                              {"OEBPS/content.opf", opf}};
  if (spec.withNav) {
    entries.push_back({"OEBPS/nav.xhtml", spec.navOverride.empty() ? navDocument(spec) : spec.navOverride});
  }
  if (spec.withNcx) {
    entries.push_back({"OEBPS/toc.ncx", spec.ncxOverride.empty() ? ncxDocument(spec) : spec.ncxOverride});
  }
  for (const auto& css : spec.cssFiles) {
    entries.push_back({"OEBPS/" + css.first, css.second});
  }
  for (int i = 1; i <= spec.chapters; i++) {
    entries.push_back({"OEBPS/ch" + std::to_string(i) + ".xhtml", chapterHtml(i)});
  }
  return buildStoredZip(entries);
}

std::vector<std::string> spineHrefs(const Epub& epub) {
  std::vector<std::string> hrefs;
  const int count = epub.getSpineItemsCount();
  for (int i = 0; i < count; i++) hrefs.push_back(epub.getSpineItem(i).href);
  return hrefs;
}

EpubSpec goodSpec(const std::string& title) {
  EpubSpec spec;
  spec.title = title;
  spec.chapters = 4;
  spec.navTocHrefs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml", "ch4.xhtml"};
  spec.ncxSrcs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml", "ch4.xhtml"};
  return spec;
}

// Counts how often one load() opens the EPUB archive itself, ignoring
// cache and retry-state reads.
size_t archiveOpensDuring(const std::string& bookPath, const std::string& cacheDir,
                          const bool buildIfMissing = true) {
  size_t archiveOpens = 0;
  SdMan.setReadObserver([&](const char* path) {
    if (path == bookPath) archiveOpens++;
  });
  Epub epub(bookPath, cacheDir);
  const bool loaded = epub.load(buildIfMissing);
  SdMan.setReadObserver(nullptr);
  return loaded ? archiveOpens : SIZE_MAX;
}

// Persisted TOC retry byte for a cache directory, or -1 when absent/unwritten.
int tocRetryState(const std::string& cachePath) {
  const std::string data = SdMan.getWrittenData(cachePath + "/toc.retry");
  return data.size() == 1 ? static_cast<unsigned char>(data[0]) : -1;
}

EpubSpec cssSpec(const std::string& title) {
  EpubSpec spec = goodSpec(title);
  // The second sheet wins the shared text-align, keeps the first sheet's
  // margin, and adds bold to the first sheet's italic class.
  spec.cssFiles = {{"styles/first.css", "p { text-align: left; margin-top: 12px; } .note { font-style: italic; }"},
                   {"styles/second.css", "p { text-align: center; } .note { font-weight: bold; }"}};
  return spec;
}

void expectCssStyles(TestUtils::TestRunner& runner, const Epub& epub, const std::string& prefix) {
  const CssParser* parser = epub.getCssParser();
  runner.expectTrue(parser != nullptr, prefix + "_ParserPresent");
  if (!parser) return;
  const CssStyle paragraph = parser->getTagStyle("p");
  runner.expectTrue(paragraph.hasTextAlign() && paragraph.textAlign == TextAlign::Center,
                    prefix + "_SecondSheetWinsTextAlign");
  runner.expectTrue(paragraph.hasMarginTop(), prefix + "_FirstSheetMarginKept");
  runner.expectFloatEq(12.0f, paragraph.marginTop.value, prefix + "_FirstSheetMarginValue");
  const CssStyle* note = parser->getStyleForClass(".note");
  runner.expectTrue(note != nullptr, prefix + "_ClassStylePresent");
  if (note) {
    runner.expectTrue(note->hasFontStyle() && note->fontStyle == CssFontStyle::Italic, prefix + "_FirstSheetItalic");
    runner.expectTrue(note->hasFontWeight() && note->fontWeight == CssFontWeight::Bold, prefix + "_SecondSheetBold");
  }
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("EpubTocRecovery");

  // ======================== A. Low-heap first open, healthy recovery ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    const std::string zip = makeEpub(goodSpec("Low Heap Book"));
    SdMan.setFileData("/books/low.epub", zip);

    Epub first("/books/low.epub", "/cacheA");
    testSetLargestFreeBlock(8 * 1024);
    runner.expectTrue(first.load(), "A_LowHeap_FirstBuild_Loads");
    runner.expectEq(4, first.getSpineItemsCount(), "A_LowHeap_SpineBuilt");
    runner.expectEq(0, first.getTocItemsCount(), "A_LowHeap_TocSkipped");
    testResetLargestFreeBlock();

    // Repeated cache-only and cover-only opens leave the recovery budget alone.
    for (int round = 0; round < 5; round++) {
      Epub coverOnly("/books/low.epub", "/cacheA");
      runner.expectTrue(coverOnly.loadMetadataOnly(), "A_CoverOnly_Loads");
      Epub cacheOnly("/books/low.epub", "/cacheA");
      runner.expectTrue(cacheOnly.load(false), "A_CacheOnly_Loads");
      runner.expectEq(0, cacheOnly.getTocItemsCount(), "A_CacheOnly_DoesNotRecover");
    }

    const auto hrefsBefore = spineHrefs(first);
    std::string preparedPath;
    runner.expectTrue(first.prepareChapterHtml(0, 11, preparedPath, nullptr, nullptr, nullptr),
                      "A_PrepareCompleteChapter");
    const std::string preparedContent = SdMan.getWrittenData(preparedPath);

    // Healthy open recovers navigation from the book file.
    Epub healthy("/books/low.epub", "/cacheA");
    runner.expectTrue(healthy.load(), "A_HealthyRecovery_Loads");
    runner.expectEq(4, healthy.getTocItemsCount(), "A_HealthyRecovery_TocRestored");
    runner.expectEqual("Low Heap Book", healthy.getTitle(), "A_HealthyRecovery_TitlePreserved");
    runner.expectEqual("Fixture Author", healthy.getAuthor(), "A_HealthyRecovery_AuthorPreserved");
    const auto hrefsAfter = spineHrefs(healthy);
    runner.expectTrue(hrefsBefore == hrefsAfter, "A_HealthyRecovery_SpinePreserved");
    runner.expectEqual(preparedContent, SdMan.getWrittenData(preparedPath), "A_HealthyRecovery_ChapterPreserved");
    bool allMapped = healthy.getTocItemsCount() == 4;
    for (int i = 0; i < healthy.getTocItemsCount(); i++) {
      if (healthy.getTocItem(i).spineIndex != i) allMapped = false;
    }
    runner.expectTrue(allMapped, "A_HealthyRecovery_TocMappedToSpine");

    // The recovered cache is complete and serves later cache-only opens.
    Epub settled("/books/low.epub", "/cacheA");
    runner.expectTrue(settled.load(false), "A_Settled_CacheOnlyLoads");
    runner.expectEq(4, settled.getTocItemsCount(), "A_Settled_TocKept");
  }

  // ======================== B. Genuine no-TOC book never retries ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec;
    spec.title = "No Toc Book";
    spec.withNav = false;
    spec.withNcx = false;
    SdMan.setFileData("/books/notoc.epub", makeEpub(spec));

    Epub first("/books/notoc.epub", "/cacheB");
    runner.expectTrue(first.load(), "B_NoToc_FirstBuild_Loads");
    runner.expectEq(0, first.getTocItemsCount(), "B_NoToc_EmptyToc");

    // Second open: no book reparse even with the zip removed.
    SdMan.remove("/books/notoc.epub");
    Epub second("/books/notoc.epub", "/cacheB");
    runner.expectTrue(second.load(), "B_NoToc_SecondLoad_WithZipRemoved");
    runner.expectEq(0, second.getTocItemsCount(), "B_NoToc_SecondLoad_EmptyToc");
    runner.expectEq(0xFF, tocRetryState(second.getCachePath()), "B_NoToc_SettledRetryState");
    Epub third("/books/notoc.epub", "/cacheB");
    runner.expectTrue(third.load(), "B_NoToc_ThirdLoad_WithZipRemoved");
    runner.expectEq(0, third.getTocItemsCount(), "B_NoToc_ThirdLoad_EmptyToc");
  }

  // ======================== C. Persistent failures stop retrying ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec = goodSpec("Broken Toc Book");
    spec.navOverride = "<html><body><nav epub:type=\"toc\"><ol><li>not xml";
    spec.ncxOverride = "<ncx><navMap><navPoint>";
    SdMan.setFileData("/books/broken.epub", makeEpub(spec));

    Epub first("/books/broken.epub", "/cacheC");
    runner.expectTrue(first.load(), "C_Broken_FirstBuild_Loads");
    runner.expectEq(0, first.getTocItemsCount(), "C_Broken_FirstBuild_EmptyToc");

    // Fresh opens retry a bounded number of times, then stop: the persisted
    // retry byte reaches the attempt limit and the cache stays intact.
    for (int open = 0; open < 10; open++) {
      Epub attempt("/books/broken.epub", "/cacheC");
      runner.expectTrue(attempt.load(), "C_Broken_OpenLoads_" + std::to_string(open));
      Epub check("/books/broken.epub", "/cacheC");
      runner.expectTrue(check.load(false), "C_Broken_CacheIntact_" + std::to_string(open));
      runner.expectEq(0, check.getTocItemsCount(), "C_Broken_EmptyTocKept_" + std::to_string(open));
      runner.expectEqual("Broken Toc Book", check.getTitle(), "C_Broken_MetadataKept_" + std::to_string(open));
    }
    runner.expectEq(3, tocRetryState(first.getCachePath()), "C_Broken_RetryLimitPersisted");
  }

  // ======================== D. Sparse nav loses to a usable NCX ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec;
    spec.title = "Sparse Nav Book";
    spec.chapters = 4;
    spec.navTocHrefs = {"ch1.xhtml"};  // one covered chapter of four spine items
    spec.ncxSrcs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml", "ch4.xhtml"};
    SdMan.setFileData("/books/sparse.epub", makeEpub(spec));

    Epub epub("/books/sparse.epub", "/cacheD");
    runner.expectTrue(epub.load(), "D_SparseNav_Loads");
    runner.expectEq(4, epub.getTocItemsCount(), "D_SparseNav_NcxSelected");
    runner.expectTrue(!epub.getTocItem(0).title.empty() && epub.getTocItem(0).title.rfind("NCX ", 0) == 0,
                      "D_SparseNav_NcxTitles");
    Epub settled("/books/sparse.epub", "/cacheD");
    runner.expectTrue(settled.load(false), "D_SparseNav_CacheOnlyLoads");
    runner.expectEq(4, settled.getTocItemsCount(), "D_SparseNav_CacheOnlyTocKept");
  }

  // ======================== E. Stale NCX does not replace a better nav ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec;
    spec.title = "Stale Ncx Book";
    spec.chapters = 4;
    spec.navTocHrefs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml"};  // three chapters, not complete
    spec.navPageListHrefs = {"ch1.xhtml#p1", "ch1.xhtml#p2"};    // must stay out of the TOC
    spec.ncxSrcs = {"old/ch1.xhtml", "old/ch2.xhtml"};           // no spine match: stale
    SdMan.setFileData("/books/stale.epub", makeEpub(spec));

    Epub epub("/books/stale.epub", "/cacheE");
    runner.expectTrue(epub.load(), "E_StaleNcx_Loads");
    runner.expectEq(3, epub.getTocItemsCount(), "E_StaleNcx_NavKept");
    runner.expectTrue(!epub.getTocItem(0).title.empty() && epub.getTocItem(0).title.rfind("Nav ", 0) == 0,
                      "E_StaleNcx_NavTitles");
    runner.expectEq(int16_t(0), epub.getTocItem(0).spineIndex, "E_StaleNcx_NavMapped");
    Epub settled("/books/stale.epub", "/cacheE");
    runner.expectTrue(settled.load(false), "E_StaleNcx_CacheOnlyLoads");
    runner.expectEq(3, settled.getTocItemsCount(), "E_StaleNcx_CacheOnlyTocKept");
  }

  // ======================== F. Failed replacement preserves the good cache ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/replace.epub", makeEpub(goodSpec("Replace Book")));

    Epub first("/books/replace.epub", "/cacheF");
    testSetLargestFreeBlock(8 * 1024);
    runner.expectTrue(first.load(), "F_FirstBuild_Loads");
    runner.expectEq(0, first.getTocItemsCount(), "F_FirstBuild_TocEmpty");

    // Fail the commit rename of the recovery rebuild (1 = build commit, 2 = final -> backup).
    SdMan.queueRenameFailure(3, RenameFailMode::FailBeforeMutation);
    testResetLargestFreeBlock();
    Epub failed("/books/replace.epub", "/cacheF");
    runner.expectTrue(failed.load(), "F_FailedReplace_Loads");
    runner.expectEq(0, failed.getTocItemsCount(), "F_FailedReplace_TocStillEmpty");
    runner.expectEqual("Replace Book", failed.getTitle(), "F_FailedReplace_MetadataKept");
    runner.expectEq(4, failed.getSpineItemsCount(), "F_FailedReplace_SpineKept");
    runner.expectTrue(SdMan.exists(failed.getCachePath() + "/book.bin"), "F_FailedReplace_CacheFileSurvives");

    Epub verify("/books/replace.epub", "/cacheF");
    runner.expectTrue(verify.load(false), "F_FailedReplace_CacheStillLoads");

    // A later open completes the recovery.
    Epub retry("/books/replace.epub", "/cacheF");
    runner.expectTrue(retry.load(), "F_RetryAfterFailure_Loads");
    runner.expectEq(4, retry.getTocItemsCount(), "F_RetryAfterFailure_TocRestored");
    Epub settled("/books/replace.epub", "/cacheF");
    runner.expectTrue(settled.load(false), "F_RetryAfterFailure_CacheOnlyLoads");
    runner.expectEq(4, settled.getTocItemsCount(), "F_RetryAfterFailure_CacheOnlyTocKept");

    // A commit rename that reports failure after moving keeps the new cache loadable.
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/replace2.epub", makeEpub(goodSpec("Replace Two Book")));
    Epub base2("/books/replace2.epub", "/cacheF2");
    testSetLargestFreeBlock(8 * 1024);
    runner.expectTrue(base2.load(), "F_AfterMutation_FirstBuild_Loads");
    SdMan.queueRenameFailure(3, RenameFailMode::FailAfterMutation);
    testResetLargestFreeBlock();
    Epub afterMutation("/books/replace2.epub", "/cacheF2");
    runner.expectTrue(afterMutation.load(), "F_AfterMutation_Loads");
    runner.expectTrue(SdMan.exists(afterMutation.getCachePath() + "/book.bin"), "F_AfterMutation_CacheFileSurvives");
    Epub afterMutationCheck("/books/replace2.epub", "/cacheF2");
    runner.expectTrue(afterMutationCheck.load(false), "F_AfterMutation_CacheStillLoads");
    runner.expectEqual("Replace Two Book", afterMutationCheck.getTitle(), "F_AfterMutation_MetadataKept");

    // A staging rename that reports failure after moving must not lose either copy.
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/replace3.epub", makeEpub(goodSpec("Replace Three Book")));
    Epub base3("/books/replace3.epub", "/cacheF3");
    testSetLargestFreeBlock(8 * 1024);
    runner.expectTrue(base3.load(), "F_StageAfterMutation_FirstBuild_Loads");
    SdMan.queueRenameFailure(2, RenameFailMode::FailAfterMutation);
    testResetLargestFreeBlock();
    Epub stageAfterMutation("/books/replace3.epub", "/cacheF3");
    runner.expectTrue(stageAfterMutation.load(), "F_StageAfterMutation_Loads");
    runner.expectEq(4, stageAfterMutation.getTocItemsCount(), "F_StageAfterMutation_TocRestored");
    runner.expectTrue(SdMan.exists(stageAfterMutation.getCachePath() + "/book.bin"),
                      "F_StageAfterMutation_CacheFileSurvives");

    // When the commit and the rollback renames both fail, the backup survives
    // and the next load restores it.
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/replace4.epub", makeEpub(goodSpec("Replace Four Book")));
    Epub base4("/books/replace4.epub", "/cacheF4");
    testSetLargestFreeBlock(8 * 1024);
    runner.expectTrue(base4.load(), "F_RollbackFailure_FirstBuild_Loads");
    SdMan.queueRenameFailure(3, RenameFailMode::FailBeforeMutation);
    SdMan.queueRenameFailure(4, RenameFailMode::FailBeforeMutation);
    testResetLargestFreeBlock();
    Epub rollbackFailure("/books/replace4.epub", "/cacheF4");
    runner.expectTrue(rollbackFailure.load(), "F_RollbackFailure_Loads");
    runner.expectEq(0, rollbackFailure.getTocItemsCount(), "F_RollbackFailure_TocStillEmpty");
    runner.expectEqual("Replace Four Book", rollbackFailure.getTitle(), "F_RollbackFailure_MetadataKept");
    runner.expectTrue(SdMan.exists(rollbackFailure.getCachePath() + "/book.bin"),
                      "F_RollbackFailure_CacheFileRestored");
    Epub rollbackRetry("/books/replace4.epub", "/cacheF4");
    runner.expectTrue(rollbackRetry.load(), "F_RollbackFailure_RetryRecovers");
    runner.expectEq(4, rollbackRetry.getTocItemsCount(), "F_RollbackFailure_RetryTocRestored");
  }

  // ======================== G. Legacy empty-TOC caches ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/legacy.epub", makeEpub(goodSpec("Different Opf Title")));

    // A valid pre-fix cache: metadata and spine, empty TOC, no retry state.
    Epub pathOwner("/books/legacy.epub", "/cacheG");
    BookMetadataCache legacy(pathOwner.getCachePath());
    runner.expectTrue(
        legacy.rebuildFromMemory({"Legacy Book", "Legacy Author", "en", "", ""},
                                 {{"OEBPS/ch1.xhtml", -1},
                                  {"OEBPS/ch2.xhtml", -1},
                                  {"OEBPS/ch3.xhtml", -1},
                                  {"OEBPS/ch4.xhtml", -1}},
                                 {}),
        "G_LegacyWithToc_CacheSeeded");

    Epub recovered("/books/legacy.epub", "/cacheG");
    runner.expectTrue(recovered.load(), "G_LegacyWithToc_Loads");
    runner.expectEq(4, recovered.getTocItemsCount(), "G_LegacyWithToc_TocRecovered");
    runner.expectEqual("Legacy Book", recovered.getTitle(), "G_LegacyWithToc_MetadataPreserved");
    Epub settled("/books/legacy.epub", "/cacheG");
    runner.expectTrue(settled.load(false), "G_LegacyWithToc_CacheOnlyLoads");
    runner.expectEq(4, settled.getTocItemsCount(), "G_LegacyWithToc_CacheOnlyTocKept");

    // A legacy no-TOC cache settles after its one classification probe.
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec;
    spec.title = "Legacy No Toc";
    spec.withNav = false;
    spec.withNcx = false;
    SdMan.setFileData("/books/legacy2.epub", makeEpub(spec));
    Epub pathOwner2("/books/legacy2.epub", "/cacheG2");
    BookMetadataCache legacy2(pathOwner2.getCachePath());
    runner.expectTrue(legacy2.rebuildFromMemory({"Legacy No Toc", "Legacy Author", "en", "", ""},
                                                {{"OEBPS/ch1.xhtml", -1},
                                                 {"OEBPS/ch2.xhtml", -1},
                                                 {"OEBPS/ch3.xhtml", -1}},
                                                {}),
                      "G_LegacyNoToc_CacheSeeded");

    Epub probe("/books/legacy2.epub", "/cacheG2");
    runner.expectTrue(probe.load(), "G_LegacyNoToc_FirstLoad");
    runner.expectEq(0, probe.getTocItemsCount(), "G_LegacyNoToc_EmptyToc");
    runner.expectEq(0xFF, tocRetryState(probe.getCachePath()), "G_LegacyNoToc_SettledRetryState");
    Epub settled2("/books/legacy2.epub", "/cacheG2");
    runner.expectTrue(settled2.load(), "G_LegacyNoToc_StaysSettledLoads");
    runner.expectEq(0, settled2.getTocItemsCount(), "G_LegacyNoToc_StaysSettled");
  }

  // ======================== H. Duplicate targets do not inflate coverage ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec;
    spec.title = "Duplicate Nav Book";
    spec.chapters = 4;
    spec.navTocHrefs = {"ch1.xhtml", "ch1.xhtml", "ch1.xhtml", "ch1.xhtml", "ch1.xhtml"};  // one chapter
    spec.ncxSrcs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml", "ch4.xhtml"};
    SdMan.setFileData("/books/dupnav.epub", makeEpub(spec));

    Epub epub("/books/dupnav.epub", "/cacheH");
    runner.expectTrue(epub.load(), "H_DuplicateNav_Loads");
    runner.expectEq(4, epub.getTocItemsCount(), "H_DuplicateNav_NcxSelected");
    runner.expectTrue(!epub.getTocItem(0).title.empty() && epub.getTocItem(0).title.rfind("NCX ", 0) == 0,
                      "H_DuplicateNav_NcxTitles");

    // The converse: many NCX rows inside one chapter must not beat a wider nav.
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec2;
    spec2.title = "Duplicate Ncx Book";
    spec2.chapters = 4;
    spec2.navTocHrefs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml"};
    spec2.ncxSrcs = {"ch1.xhtml", "ch1.xhtml", "ch1.xhtml", "ch1.xhtml", "ch1.xhtml", "ch1.xhtml"};
    SdMan.setFileData("/books/dupncx.epub", makeEpub(spec2));

    Epub epub2("/books/dupncx.epub", "/cacheH2");
    runner.expectTrue(epub2.load(), "H_DuplicateNcx_Loads");
    runner.expectEq(3, epub2.getTocItemsCount(), "H_DuplicateNcx_NavKept");
    runner.expectTrue(!epub2.getTocItem(0).title.empty() && epub2.getTocItem(0).title.rfind("Nav ", 0) == 0,
                      "H_DuplicateNcx_NavTitles");
  }

  // ======================== I. Partial output from a failed parse never competes ========================
  {
    // The nav emits four mapped rows, then breaks: the valid NCX must win.
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec;
    spec.title = "Broken Nav Book";
    spec.chapters = 4;
    spec.ncxSrcs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml", "ch4.xhtml"};
    spec.navOverride =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n"
        "<body><nav epub:type=\"toc\"><ol>"
        "<li><a href=\"ch1.xhtml\">A</a></li><li><a href=\"ch2.xhtml\">B</a></li>"
        "<li><a href=\"ch3.xhtml\">C</a></li><li><a href=\"ch4.xhtml\">D</a></li>"
        "<li><a href=\"ch1";
    SdMan.setFileData("/books/brokennav.epub", makeEpub(spec));

    Epub epub("/books/brokennav.epub", "/cacheI");
    runner.expectTrue(epub.load(), "I_BrokenNav_Loads");
    runner.expectEq(4, epub.getTocItemsCount(), "I_BrokenNav_NcxSelected");
    runner.expectTrue(!epub.getTocItem(0).title.empty() && epub.getTocItem(0).title.rfind("NCX ", 0) == 0,
                      "I_BrokenNav_NcxTitles");

    // The NCX emits four mapped rows, then breaks: the valid nav must win.
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec2;
    spec2.title = "Broken Ncx Book";
    spec2.chapters = 4;
    spec2.navTocHrefs = {"ch1.xhtml", "ch2.xhtml", "ch3.xhtml"};
    spec2.ncxOverride =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><navMap>"
        "<navPoint><navLabel><text>N1</text></navLabel><content src=\"ch1.xhtml\"/></navPoint>"
        "<navPoint><navLabel><text>N2</text></navLabel><content src=\"ch2.xhtml\"/></navPoint>"
        "<navPoint><navLabel><text>N3</text></navLabel><content src=\"ch3.xhtml\"/></navPoint>"
        "<navPoint><navLabel><text>N4</text></navLabel><content src=\"ch4.xhtml\"/></navPoint>"
        "<navPoint><navLabel";
    SdMan.setFileData("/books/brokenncx.epub", makeEpub(spec2));

    Epub epub2("/books/brokenncx.epub", "/cacheI2");
    runner.expectTrue(epub2.load(), "I_BrokenNcx_Loads");
    runner.expectEq(3, epub2.getTocItemsCount(), "I_BrokenNcx_NavKept");
    runner.expectTrue(!epub2.getTocItem(0).title.empty() && epub2.getTocItem(0).title.rfind("Nav ", 0) == 0,
                      "I_BrokenNcx_NavTitles");
  }

  // ======================== S. Retry-state faults defer without parsing the archive ========================
  {
    // An existing but unreadable state file must not authorize recovery.
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/statefault.epub", makeEpub(goodSpec("State Fault Book")));
    Epub pathOwner("/books/statefault.epub", "/cacheS");
    BookMetadataCache seeded(pathOwner.getCachePath());
    runner.expectTrue(seeded.rebuildFromMemory({"State Fault Book", "State Author", "en", "", ""},
                                               {{"OEBPS/ch1.xhtml", -1},
                                                {"OEBPS/ch2.xhtml", -1},
                                                {"OEBPS/ch3.xhtml", -1},
                                                {"OEBPS/ch4.xhtml", -1}},
                                               {}),
                      "S_UnreadableState_CacheSeeded");
    SdMan.registerFile(pathOwner.getCachePath() + "/toc.retry", "");

    Epub unreadable("/books/statefault.epub", "/cacheS");
    runner.expectTrue(unreadable.load(), "S_UnreadableState_Loads");
    runner.expectEq(0, unreadable.getTocItemsCount(), "S_UnreadableState_TocEmpty");
    runner.expectEqual("State Fault Book", unreadable.getTitle(), "S_UnreadableState_MetadataUsable");
    Epub settled("/books/statefault.epub", "/cacheS");
    runner.expectTrue(settled.load(false), "S_UnreadableState_CacheOnlyLoads");
    runner.expectEq(0, settled.getTocItemsCount(), "S_UnreadableState_CacheOnlyTocEmpty");

    // A failed attempt-reservation write defers the recovery to a later open.
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/statefault2.epub", makeEpub(goodSpec("State Fault Two Book")));
    Epub pathOwner2("/books/statefault2.epub", "/cacheS2");
    BookMetadataCache seeded2(pathOwner2.getCachePath());
    runner.expectTrue(seeded2.rebuildFromMemory({"State Fault Two Book", "State Author", "en", "", ""},
                                                {{"OEBPS/ch1.xhtml", -1},
                                                 {"OEBPS/ch2.xhtml", -1},
                                                 {"OEBPS/ch3.xhtml", -1},
                                                 {"OEBPS/ch4.xhtml", -1}},
                                                {}),
                      "S_WriteFailure_CacheSeeded");
    SdMan.setSyncResult(false);
    Epub writeFailure("/books/statefault2.epub", "/cacheS2");
    runner.expectTrue(writeFailure.load(), "S_WriteFailure_Loads");
    runner.expectEq(0, writeFailure.getTocItemsCount(), "S_WriteFailure_TocEmpty");
    // The failed reservation defers recovery; the next healthy open recovers.
    SdMan.setSyncResult(true);

    Epub later("/books/statefault2.epub", "/cacheS2");
    runner.expectTrue(later.load(), "S_WriteFailure_LaterRecovers");
    runner.expectEq(4, later.getTocItemsCount(), "S_WriteFailure_LaterTocRestored");
  }

  {
    SdMan.reset();
    testResetLargestFreeBlock();
    const std::string path = "/books/old-layout.epub";
    const std::string zip = makeEpub(goodSpec("Original spine"));
    SdMan.registerFile(path, zip);
    Epub epub(path, "/cacheVersion");
    {
      BookMetadataCache old(epub.getCachePath());
      runner.expectTrue(old.rebuildFromMemory({"Old split", "", "", "", ""},
                                             {{epub.getCachePath() + "/sections/0_0.html", -1}}, {}),
                        "Create obsolete virtual spine fixture");
    }
    const std::string metadataPath = epub.getCachePath() + "/book.bin";
    std::string previous = SdMan.getWrittenData(metadataPath);
    previous[0] = 7;
    SdMan.setFileData(metadataPath, previous);
    const std::vector<std::pair<std::string, std::string>> userFiles = {
        {path, zip}, {epub.getCachePath() + "/progress.bin", "PROGRESS"},
        {epub.getCachePath() + "/bookmarks.bin", "BOOKMARKS"}, {"/.papyrix/settings.bin", "SETTINGS"}};
    for (const auto& entry : userFiles) SdMan.registerFile(entry.first, entry.second);
    runner.expectFalse(epub.load(false), "Reject previous virtual spine metadata");
    runner.expectTrue(epub.load(), "Rebuild original spine from EPUB");
    runner.expectEq(4, epub.getSpineItemsCount(), "Rebuild retains original spine count");
    runner.expectEqual("OEBPS/ch1.xhtml", epub.getSpineItem(0).href, "Rebuild restores original chapter href");
    for (const auto& entry : userFiles) {
      FsFile file = SdMan.open(entry.first.c_str());
      std::string bytes(entry.second.size(), '\0');
      runner.expectEq(static_cast<int>(bytes.size()), file.read(&bytes[0], bytes.size()), "User file remains readable");
      runner.expectEqual(entry.second, bytes, "Metadata invalidation preserves user data");
    }
  }

  // ======================== J. External CSS on fresh and cached opens ========================
  {
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/styled.epub", makeEpub(cssSpec("Styled Book")));

    Epub fresh("/books/styled.epub", "/cacheJ");
    runner.expectTrue(fresh.load(), "J_Fresh_Loads");
    expectCssStyles(runner, fresh, "J_Fresh");
    runner.expectEq(4, fresh.getTocItemsCount(), "J_Fresh_TocBuilt");

    // User data in the cache directory must survive a cached open.
    const std::vector<std::pair<std::string, std::string>> userFiles = {
        {fresh.getCachePath() + "/progress.bin", "PROGRESS"}, {fresh.getCachePath() + "/bookmarks.bin", "BOOKMARKS"}};
    for (const auto& entry : userFiles) SdMan.registerFile(entry.first, entry.second);

    Epub cached("/books/styled.epub", "/cacheJ");
    runner.expectTrue(cached.load(), "J_Cached_Loads");
    expectCssStyles(runner, cached, "J_Cached");
    runner.expectEqual("Styled Book", cached.getTitle(), "J_Cached_TitlePreserved");
    runner.expectEq(4, cached.getSpineItemsCount(), "J_Cached_SpinePreserved");
    runner.expectEq(4, cached.getTocItemsCount(), "J_Cached_TocPreserved");
    for (const auto& entry : userFiles) {
      FsFile file = SdMan.open(entry.first.c_str());
      std::string bytes(entry.second.size(), '\0');
      runner.expectEq(static_cast<int>(bytes.size()), file.read(&bytes[0], bytes.size()),
                      "J_Cached_UserFileReadable");
      runner.expectEqual(entry.second, bytes, "J_Cached_UserDataPreserved");
    }

    // Cache-only and metadata-only opens stay light: no source parse, no CSS.
    runner.expectEq(size_t(0), archiveOpensDuring("/books/styled.epub", "/cacheJ", false),
                    "J_CacheOnly_NoArchiveParse");
    Epub cacheOnly("/books/styled.epub", "/cacheJ");
    runner.expectTrue(cacheOnly.load(false), "J_CacheOnly_Loads");
    runner.expectTrue(cacheOnly.getCssParser() == nullptr, "J_CacheOnly_NoCssParser");
    runner.expectEq(4, cacheOnly.getTocItemsCount(), "J_CacheOnly_TocPreserved");
    Epub metadataOnly("/books/styled.epub", "/cacheJ");
    runner.expectTrue(metadataOnly.loadMetadataOnly(), "J_MetadataOnly_Loads");
    runner.expectTrue(metadataOnly.getCssParser() == nullptr, "J_MetadataOnly_NoCssParser");

    // A low-heap cached open keeps the book usable without styles, and a
    // later healthy open still loads them.
    Epub lowHeap("/books/styled.epub", "/cacheJ");
    testSetLargestFreeBlock(8 * 1024);
    runner.expectTrue(lowHeap.load(), "J_LowHeapCached_Loads");
    testResetLargestFreeBlock();
    runner.expectTrue(lowHeap.getCssParser() == nullptr || !lowHeap.getCssParser()->hasStyles(),
                      "J_LowHeapCached_NoStyles");
    runner.expectEq(4, lowHeap.getTocItemsCount(), "J_LowHeapCached_TocPreserved");
    Epub healthy("/books/styled.epub", "/cacheJ");
    runner.expectTrue(healthy.load(), "J_HealthyAfterLowHeap_Loads");
    expectCssStyles(runner, healthy, "J_HealthyAfterLowHeap");

    // A missing EPUB source must not destroy the valid metadata cache.
    SdMan.remove("/books/styled.epub");
    Epub sourceGone("/books/styled.epub", "/cacheJ");
    runner.expectTrue(sourceGone.load(), "J_SourceGone_LoadsFromCache");
    runner.expectEqual("Styled Book", sourceGone.getTitle(), "J_SourceGone_TitlePreserved");
    runner.expectEq(4, sourceGone.getSpineItemsCount(), "J_SourceGone_SpinePreserved");
    runner.expectEq(4, sourceGone.getTocItemsCount(), "J_SourceGone_TocPreserved");
    runner.expectTrue(sourceGone.getCssParser() == nullptr || !sourceGone.getCssParser()->hasStyles(),
                      "J_SourceGone_NoStyles");
    Epub sourceGoneCheck("/books/styled.epub", "/cacheJ");
    runner.expectTrue(sourceGoneCheck.load(false), "J_SourceGone_CacheStillLoads");
  }

  // ======================== K. CSS failures stay nonfatal ========================
  {
    // A declared stylesheet with no stored entry does not block the others.
    SdMan.reset();
    testResetLargestFreeBlock();
    EpubSpec spec = cssSpec("Missing Css Book");
    spec.missingCssFiles = {"styles/missing.css"};
    SdMan.setFileData("/books/missingcss.epub", makeEpub(spec));

    Epub fresh("/books/missingcss.epub", "/cacheK");
    runner.expectTrue(fresh.load(), "K_Missing_FreshLoads");
    expectCssStyles(runner, fresh, "K_Missing_Fresh");
    Epub cached("/books/missingcss.epub", "/cacheK");
    runner.expectTrue(cached.load(), "K_Missing_CachedLoads");
    expectCssStyles(runner, cached, "K_Missing_Cached");
    Epub check("/books/missingcss.epub", "/cacheK");
    runner.expectTrue(check.load(false), "K_Missing_CacheStillLoads");
    runner.expectEqual("Missing Css Book", check.getTitle(), "K_Missing_MetadataKept");

    // TOC recovery and CSS share the OPF parse on the same cached open.
    SdMan.reset();
    testResetLargestFreeBlock();
    SdMan.setFileData("/books/recovercss.epub", makeEpub(cssSpec("Recover Css Book")));
    Epub lowFirst("/books/recovercss.epub", "/cacheK2");
    testSetLargestFreeBlock(8 * 1024);
    runner.expectTrue(lowFirst.load(), "K_Recovery_FirstBuildLoads");
    runner.expectEq(0, lowFirst.getTocItemsCount(), "K_Recovery_TocSkipped");
    testResetLargestFreeBlock();
    Epub recovered("/books/recovercss.epub", "/cacheK2");
    runner.expectTrue(recovered.load(), "K_Recovery_Loads");
    runner.expectEq(4, recovered.getTocItemsCount(), "K_Recovery_TocRestored");
    expectCssStyles(runner, recovered, "K_Recovery");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
