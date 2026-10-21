#!/usr/bin/env python3
"""Check that failed or partial EPUB section copies never become success."""

import subprocess
import sys
import tempfile
from pathlib import Path

from test_reader_resume import block

ROOT = Path(__file__).resolve().parents[2]


HARNESS = r'''
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "Html5Normalizer.h"
#include "SDCardManager.h"

#define TAG "TEST"
template <class... T> void logMessage(T...) {}
#define LOG_DBG(...) logMessage(__VA_ARGS__)
#define LOG_INF(...) logMessage(__VA_ARGS__)
#define LOG_ERR(...) logMessage(__VA_ARGS__)


// Shell for the production Epub members used by extractSections.
class Epub {
 public:
  struct SectionRange {
    uint32_t startOffset = 0;
    uint32_t endOffset = 0;
  };
  struct SpineSplit {
    int originalSpineIndex = -1;
    std::string originalHref;
    std::string chapterBasePath;
    std::string headHtml;
    std::vector<SectionRange> sections;
    std::unordered_map<std::string, uint32_t> anchorByteOffsets;
  };
  std::string cachePath;
  std::string sectionFilePath(int originalSpineIndex, int sectionIndex) const;
  bool extractSections(const std::string& htmlPath, const SpineSplit& split, uint8_t* ioBuf);
};

@sections@

void check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

const char* SRC_PATH = "/cache/.tmp_split_0.html";
const std::string SRC = "0123456789abcdefghij";
const std::string WRAPPER =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n";
const std::string BODY_OPEN = "<body><div>\n";
const std::string BODY_CLOSE = "\n</div></body>\n</html>\n";

std::string sectionPath(int index) { return "/cache/sections/0_" + std::to_string(index) + ".html"; }

std::string readFile(const std::string& path) {
  FsFile file = SdMan.open(path.c_str(), O_RDONLY);
  return file.getBuffer();
}

std::string expectedSection(size_t index, const std::string& headHtml) {
  return WRAPPER + headHtml + (headHtml.empty() ? "" : "\n") + BODY_OPEN + SRC.substr(index * 10, 10) + BODY_CLOSE;
}

Epub::SpineSplit makeSplit(const std::vector<Epub::SectionRange>& ranges, const std::string& basePath,
                           const std::string& headHtml) {
  Epub::SpineSplit split;
  split.originalSpineIndex = 0;
  split.chapterBasePath = basePath;
  split.headHtml = headHtml;
  split.sections = ranges;
  return split;
}

bool runExtract(const Epub::SpineSplit& split) {
  Epub epub;
  epub.cachePath = "/cache";
  uint8_t ioBuf[4096];
  const bool result = epub.extractSections(SRC_PATH, split, ioBuf);
  check(readFile(SRC_PATH) == SRC, "section copies keep the source unchanged");
  return result;
}

void resetSd() {
  SdMan.reset();
  SdMan.registerFile(SRC_PATH, SRC);
}

int main() {
  const std::vector<Epub::SectionRange> twoSections = {{0, 10}, {10, 20}};

  resetSd();
  check(runExtract(makeSplit(twoSections, "ch01/", "HEAD")), "complete extract succeeds");
  check(readFile(sectionPath(0)) == expectedSection(0, "HEAD"), "section 0 holds wrapper, head and exact bytes");
  check(readFile(sectionPath(1)) == expectedSection(1, "HEAD"), "section 1 holds wrapper, head and exact bytes");
  check(readFile("/cache/sections/0.base") == "ch01/", "base path sidecar holds chapter base");
  check(!SdMan.exists(sectionPath(0) + ".norm"), "success leaves no normalize temp");

  resetSd();
  SdMan.setFailBufferReadAfter(0);
  check(!runExtract(makeSplit(twoSections, "", "")), "negative read fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "negative read leaves no section files");

  resetSd();
  SdMan.setReadLimit(5);
  check(!runExtract(makeSplit(twoSections, "", "")), "read that ends early fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "short read leaves no section files");

  resetSd();
  check(!runExtract(makeSplit({{100, 110}}, "", "")), "seek past source end fails the extract");
  check(!SdMan.exists(sectionPath(0)), "seek failure leaves no section file");
  resetSd();
  check(!runExtract(makeSplit({{21, 21}}, "", "")), "an empty range beyond the source fails");
  resetSd();
  check(!runExtract(makeSplit({{15, 10}}, "", "")), "reversed offsets fail the extract");

  resetSd();
  check(!runExtract(makeSplit({{0, 10}, {10, 21}}, "", "")), "a later invalid range fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "failure removes sections from this call");

  resetSd();
  SdMan.setWriteLimit(8);
  check(!runExtract(makeSplit(twoSections, "", "")), "short section write fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "short write leaves no section files");

  resetSd();
  SdMan.setWriteLimit(3);
  check(!runExtract(makeSplit(twoSections, "ch01/", "")), "failed sidecar write fails the extract");
  check(!SdMan.exists("/cache/sections/0.base"), "failed sidecar leaves no base file");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "failed sidecar leaves no section files");

  resetSd();
  SdMan.registerFile("/cache/sections/0.base", "ch01/");
  SdMan.setWriteLimit(3);
  check(!runExtract(makeSplit(twoSections, "ch01/", "")), "section write failure keeps the existing base");
  check(readFile("/cache/sections/0.base") == "ch01/", "a failed retry preserves the existing sidecar");

  resetSd();
  SdMan.registerFile("/cache/sections/0.base", "different/");
  check(!runExtract(makeSplit(twoSections, "ch01/", "")), "a different existing base fails the extract");
  check(readFile("/cache/sections/0.base") == "different/", "an incompatible sidecar remains unchanged");

  resetSd();
  SdMan.setSyncResult(false);
  check(!runExtract(makeSplit(twoSections, "", "")), "failed sync fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "failed sync leaves no section files");

  resetSd();
  SdMan.setOpenFileForReadFailCount(1);
  check(!runExtract(makeSplit(twoSections, "", "")), "source open failure fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "source open failure leaves no section files");

  resetSd();
  SdMan.registerFile(sectionPath(1), "OLD-COMPLETE");
  SdMan.setFailBufferReadAfter(0);
  check(!runExtract(makeSplit(twoSections, "", "")), "failed retry reports failure");
  check(!SdMan.exists(sectionPath(0)), "failed retry removes only its own partial output");
  check(readFile(sectionPath(1)) == "OLD-COMPLETE", "preexisting section file survives a failed retry");

  resetSd();
  SdMan.setSyncResult(false);
  check(!runExtract(makeSplit(twoSections, "", "")), "first attempt fails");
  SdMan.setSyncResult(true);
  check(runExtract(makeSplit(twoSections, "", "")), "retry after failure succeeds");
  check(readFile(sectionPath(0)) == expectedSection(0, ""), "retry writes complete section 0");
  check(readFile(sectionPath(1)) == expectedSection(1, ""), "retry writes complete section 1");

  resetSd();
  SdMan.queueRenameFailure(1, RenameFailMode::FailBeforeMutation);
  check(!runExtract(makeSplit(twoSections, "", "")), "failed normalize replace fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "failed replace leaves no section files");
  check(!SdMan.exists(sectionPath(0) + ".norm") && !SdMan.exists(sectionPath(1) + ".norm"),
        "failed replace leaves no normalize temp");

  resetSd();
  SdMan.queueRenameFailure(1, RenameFailMode::FailAfterMutation);
  check(!runExtract(makeSplit(twoSections, "", "")), "rename failure after mutation fails the extract");
  check(!SdMan.exists(sectionPath(0)) && !SdMan.exists(sectionPath(1)), "post-mutation failure leaves no output");

  puts("PASS: section copies verify every read, seek, write and sync; failures remove only owned outputs;"
       " retries and preexisting files stay intact");
}
'''


def main():
    source_root = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT
    epub = (source_root / "lib/Epub/src/Epub.cpp").read_text()
    sections = [
        block(epub, "std::string Epub::sectionFilePath("),
        block(epub, "bool Epub::extractSections("),
    ]
    harness = HARNESS.replace("@sections@", "\n".join(sections))
    with tempfile.TemporaryDirectory(prefix="papyrix-epub-section-copy-") as directory:
        path = Path(directory)
        source = path / "section_copy.cpp"
        source.write_text(harness)
        binary = path / "section_copy"
        subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Wextra", "-I", str(source_root / "test/mocks"),
             "-I", str(source_root / "lib/Memory/src"), "-I", str(source_root / "lib/Html5/src"),
             str(source), str(source_root / "lib/Html5/src/Html5Normalizer.cpp"), "-o", str(binary)],
            check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
