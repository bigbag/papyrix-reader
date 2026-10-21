// Production Epub::load() and BookMetadataCache TOC recovery checks. Uses real
// stored-entry EPUB archives on the mock SD card.
#include <Epub.h>
#include <HtmlSplitter.h>
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

// Counts how often one full load() opens the EPUB archive itself, ignoring
// cache and retry-state reads.
size_t archiveOpensDuring(const std::string& bookPath, const std::string& cacheDir) {
  size_t archiveOpens = 0;
  SdMan.setReadObserver([&](const char* path) {
    if (path == bookPath) archiveOpens++;
  });
  Epub epub(bookPath, cacheDir);
  const bool loaded = epub.load();
  SdMan.setReadObserver(nullptr);
  return loaded ? archiveOpens : SIZE_MAX;
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

    // A split spine sidecar must survive the recovery.
    const std::string sections = first.getCachePath() + "/sections/";
    std::string body;
    for (int i = 0; i < 40; ++i) body += "<p>" + std::string(1000, 'x') + "</p>\n";
    SdMan.registerFile(sections + "0.body", body);
    runner.expectTrue(html5::scanSectionIndex(sections + "0.body", sections + "0.idx", 0, body.size(), 24 * 1024, ""),
                      "A_Split_IndexCreated");
    Epub splitBefore("/books/low.epub", "/cacheA");
    const int sectionsBefore = splitBefore.load(false) ? splitBefore.getVirtualSectionCount(0) : -1;
    runner.expectTrue(sectionsBefore >= 2, "A_Split_MultipleSections");

    // Healthy open recovers navigation from the book file.
    Epub healthy("/books/low.epub", "/cacheA");
    runner.expectTrue(healthy.load(), "A_HealthyRecovery_Loads");
    runner.expectEq(4, healthy.getTocItemsCount(), "A_HealthyRecovery_TocRestored");
    runner.expectEqual("Low Heap Book", healthy.getTitle(), "A_HealthyRecovery_TitlePreserved");
    runner.expectEqual("Fixture Author", healthy.getAuthor(), "A_HealthyRecovery_AuthorPreserved");
    const auto hrefsBefore = spineHrefs(splitBefore);
    const auto hrefsAfter = spineHrefs(healthy);
    runner.expectTrue(hrefsBefore == hrefsAfter, "A_HealthyRecovery_SpinePreserved");
    runner.expectEq(sectionsBefore, healthy.getVirtualSectionCount(0), "A_HealthyRecovery_SplitStatePreserved");
    bool allMapped = healthy.getTocItemsCount() == 4;
    for (int i = 0; i < healthy.getTocItemsCount(); i++) {
      if (healthy.getTocItem(i).spineIndex != i) allMapped = false;
    }
    runner.expectTrue(allMapped, "A_HealthyRecovery_TocMappedToSpine");

    // The recovered cache is complete: later opens do not parse the archive.
    runner.expectEq(size_t(0), archiveOpensDuring("/books/low.epub", "/cacheA"), "A_Settled_NoArchiveParse");
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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/notoc.epub", "/cacheB"), "B_NoToc_ThirdLoad_NoArchiveParse");
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

    // Fresh opens retry a bounded number of times, then stop parsing the book.
    bool retryHappened = false;
    bool previousParsed = false;
    bool lastParsed = false;
    for (int open = 0; open < 10; open++) {
      const size_t opens = archiveOpensDuring("/books/broken.epub", "/cacheC");
      const bool parsed = opens > 0 && opens != SIZE_MAX;
      if (parsed) retryHappened = true;
      previousParsed = lastParsed;
      lastParsed = parsed;
      Epub check("/books/broken.epub", "/cacheC");
      runner.expectTrue(check.load(false), "C_Broken_CacheIntact_" + std::to_string(open));
      runner.expectEq(0, check.getTocItemsCount(), "C_Broken_EmptyTocKept_" + std::to_string(open));
      runner.expectEqual("Broken Toc Book", check.getTitle(), "C_Broken_MetadataKept_" + std::to_string(open));
    }
    runner.expectTrue(retryHappened, "C_Broken_RetriedAcrossOpens");
    runner.expectFalse(lastParsed, "C_Broken_LastOpen_NoArchiveParse");
    runner.expectFalse(previousParsed, "C_Broken_RetriesStopBeforeEnd");
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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/sparse.epub", "/cacheD"),
                    "D_SparseNav_NoArchiveParseAfterBuild");
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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/stale.epub", "/cacheE"),
                    "E_StaleNcx_NoArchiveParseAfterBuild");
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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/replace.epub", "/cacheF"),
                    "F_RetryAfterFailure_NoArchiveParse");

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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/legacy.epub", "/cacheG"),
                    "G_LegacyWithToc_NoArchiveParseAfter");

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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/legacy2.epub", "/cacheG2"), "G_LegacyNoToc_Settles");
    runner.expectEq(size_t(0), archiveOpensDuring("/books/legacy2.epub", "/cacheG2"), "G_LegacyNoToc_StaysSettled");
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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/statefault.epub", "/cacheS"),
                    "S_UnreadableState_NoArchiveParse");

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
    runner.expectEq(size_t(0), archiveOpensDuring("/books/statefault2.epub", "/cacheS2"),
                    "S_WriteFailure_NoArchiveParse");
    SdMan.setSyncResult(true);

    Epub later("/books/statefault2.epub", "/cacheS2");
    runner.expectTrue(later.load(), "S_WriteFailure_LaterRecovers");
    runner.expectEq(4, later.getTocItemsCount(), "S_WriteFailure_LaterTocRestored");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
