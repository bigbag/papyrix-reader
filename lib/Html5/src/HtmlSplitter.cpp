#include "HtmlSplitter.h"

#include <SDCardManager.h>

#include <cstring>
#include <vector>

#ifdef ARDUINO
#include <Arduino.h>
#define SPLIT_YIELD() delay(1)
#else
#define SPLIT_YIELD() ((void)0)
#endif

#include "Html5Normalizer.h"

namespace html5 {

// ============================================================================
// Void elements (no closing tag)
// ============================================================================

static bool isVoidElement(const char* name, size_t len) {
  struct Entry {
    const char* name;
    size_t len;
  };
  static const Entry voids[] = {
      {"br", 2},  {"hr", 2},  {"img", 3},    {"input", 5}, {"meta", 4},   {"link", 4},   {"area", 4},
      {"base", 4}, {"col", 3}, {"embed", 5}, {"param", 5}, {"source", 6}, {"track", 5}, {"wbr", 3},
  };
  for (const auto& v : voids) {
    if (len == v.len && memcmp(name, v.name, len) == 0) return true;
  }
  return false;
}

// ============================================================================
// findHtmlHeadAndBody — scan for <head>...</head> and <body...> offsets
// ============================================================================

static bool matchAt(const uint8_t* buf, size_t bufLen, size_t pos, const char* pattern, size_t patLen) {
  if (pos + patLen > bufLen) return false;
  return memcmp(buf + pos, pattern, patLen) == 0;
}

bool findHtmlHeadAndBody(const std::string& htmlPath, std::string& headHtml, size_t& bodyContentStart,
                         size_t& bodyContentEnd) {
  headHtml.clear();
  bodyContentStart = 0;
  bodyContentEnd = 0;

  FsFile file;
  if (!SdMan.openFileForRead("HSPLIT", htmlPath, file)) return false;

  const size_t fileSize = file.size();

  constexpr size_t kBufSize = 512;
  uint8_t buf[kBufSize];
  size_t fileOffset = 0;
  size_t headStart = SIZE_MAX;
  size_t headEnd = SIZE_MAX;
  size_t bodyGt = SIZE_MAX;
  size_t carry = 0;

  while (file.available() > 0) {
    const size_t n = file.read(buf + carry, kBufSize - carry);
    if (n == 0) break;
    const size_t total = carry + n;

    for (size_t i = 0; i < total; i++) {
      const size_t absPos = fileOffset + i;

      if (headStart == SIZE_MAX && i + 5 <= total && memcmp(buf + i, "<head", 5) == 0) {
        headStart = absPos;
      }
      if (headStart != SIZE_MAX && headEnd == SIZE_MAX && i + 7 <= total && memcmp(buf + i, "</head>", 7) == 0) {
        headEnd = absPos + 7;
      }
      if (bodyGt == SIZE_MAX && i + 5 <= total && memcmp(buf + i, "<body", 5) == 0) {
        for (size_t j = i + 5; j < total; j++) {
          if (buf[j] == '>') {
            bodyGt = fileOffset + j + 1;
            break;
          }
        }
      }
    }

    if (bodyGt != SIZE_MAX) break;

    constexpr size_t kOverlap = 16;
    if (total > kOverlap) {
      fileOffset += total - kOverlap;
      memmove(buf, buf + total - kOverlap, kOverlap);
      carry = kOverlap;
    } else {
      fileOffset += total;
      carry = 0;
    }
  }

  if (headStart != SIZE_MAX && headEnd != SIZE_MAX && headEnd > headStart) {
    const size_t headSize = headEnd - headStart;
    headHtml.resize(headSize);
    file.seek(static_cast<uint32_t>(headStart));
    file.read(reinterpret_cast<uint8_t*>(&headHtml[0]), headSize);
  }

  if (bodyGt == SIZE_MAX) {
    file.close();
    return false;
  }
  bodyContentStart = bodyGt;

  // Scan backwards from EOF for </body> — it's always near the end
  size_t bodyClose = SIZE_MAX;
  constexpr size_t kTailSize = 512;
  const size_t tailStart = (fileSize > kTailSize) ? (fileSize - kTailSize) : 0;
  file.seek(static_cast<uint32_t>(tailStart));
  uint8_t tailBuf[kTailSize];
  const size_t tailRead = file.read(tailBuf, fileSize - tailStart);
  for (size_t i = 0; i + 7 <= tailRead; i++) {
    if (memcmp(tailBuf + i, "</body>", 7) == 0) {
      bodyClose = tailStart + i;
      break;
    }
  }
  file.close();

  bodyContentEnd = (bodyClose != SIZE_MAX) ? bodyClose : fileSize;
  return true;
}

// ============================================================================
// Tag tracking state machine
// ============================================================================

enum class TState {
  Normal,
  InTag,
  InTagName,
  InClosingTagName,
  InAttrs,
  InDQuote,
  InSQuote,
  InBangDash,
  InComment,
  InDecl
};

struct TagTracker {
  TState state = TState::Normal;
  std::vector<std::string> stack;

  char tagNameBuf[64];
  size_t tagNameLen = 0;
  char currentTagBuf[256];
  size_t currentTagLen = 0;
  bool isSelfClosing = false;
  bool isClosing = false;
  int commentDashCount = 0;

  void reset() {
    state = TState::Normal;
    stack.clear();
    tagNameLen = 0;
    currentTagLen = 0;
    isSelfClosing = false;
    isClosing = false;
    commentDashCount = 0;
  }

  void appendTagByte(uint8_t b) {
    if (currentTagLen < sizeof(currentTagBuf) - 1) {
      currentTagBuf[currentTagLen++] = static_cast<char>(b);
    }
  }

  void processByte(uint8_t b) {
    switch (state) {
      case TState::Normal:
        if (b == '<') {
          state = TState::InTag;
          currentTagLen = 0;
          appendTagByte(b);
          tagNameLen = 0;
          isSelfClosing = false;
          isClosing = false;
        }
        break;

      case TState::InTag:
        appendTagByte(b);
        if (b == '/') {
          isClosing = true;
          state = TState::InClosingTagName;
        } else if (b == '!') {
          state = TState::InBangDash;
        } else if (b == '?') {
          state = TState::InDecl;
        } else {
          tagNameBuf[0] = static_cast<char>(b);
          tagNameLen = 1;
          state = TState::InTagName;
        }
        break;

      case TState::InBangDash:
        if (b == '-') {
          state = TState::InComment;
          commentDashCount = 0;
        } else {
          state = TState::InDecl;
        }
        break;

      case TState::InComment:
        if (b == '-') {
          commentDashCount++;
        } else if (b == '>' && commentDashCount >= 2) {
          state = TState::Normal;
        } else {
          commentDashCount = 0;
        }
        break;

      case TState::InDecl:
        if (b == '>') state = TState::Normal;
        break;

      case TState::InTagName:
        appendTagByte(b);
        if (b == '>') {
          finishTag();
          state = TState::Normal;
        } else if (b == ' ' || b == '\t' || b == '\n' || b == '\r') {
          state = TState::InAttrs;
        } else if (b == '/') {
          isSelfClosing = true;
          state = TState::InAttrs;
        } else {
          if (tagNameLen < sizeof(tagNameBuf) - 1) {
            tagNameBuf[tagNameLen++] = static_cast<char>(b);
          }
        }
        break;

      case TState::InClosingTagName:
        appendTagByte(b);
        if (b == '>') {
          popTag();
          state = TState::Normal;
        } else if (b != ' ' && b != '\t' && b != '\n' && b != '\r') {
          if (tagNameLen < sizeof(tagNameBuf) - 1) {
            tagNameBuf[tagNameLen++] = static_cast<char>(b);
          }
        }
        break;

      case TState::InAttrs:
        appendTagByte(b);
        if (b == '>') {
          if (!isClosing) finishTag();
          state = TState::Normal;
        } else if (b == '"') {
          state = TState::InDQuote;
        } else if (b == '\'') {
          state = TState::InSQuote;
        } else if (b == '/') {
          isSelfClosing = true;
        }
        break;

      case TState::InDQuote:
        appendTagByte(b);
        if (b == '"') state = TState::InAttrs;
        break;

      case TState::InSQuote:
        appendTagByte(b);
        if (b == '\'') state = TState::InAttrs;
        break;
    }
  }

  bool inNormalState() const { return state == TState::Normal; }

 private:
  void finishTag() {
    if (isSelfClosing || isClosing) return;
    tagNameBuf[tagNameLen] = '\0';
    if (isVoidElement(tagNameBuf, tagNameLen)) return;
    stack.push_back(std::string(currentTagBuf, currentTagLen));
  }

  void popTag() {
    tagNameBuf[tagNameLen] = '\0';
    for (int i = static_cast<int>(stack.size()) - 1; i >= 0; i--) {
      const std::string& entry = stack[static_cast<size_t>(i)];
      // Extract tag name from stored opening tag "<tagname ..." or "<tagname>"
      size_t nameStart = 1;  // skip '<'
      size_t nameEnd = nameStart;
      while (nameEnd < entry.size() && entry[nameEnd] != ' ' && entry[nameEnd] != '>' && entry[nameEnd] != '/' &&
             entry[nameEnd] != '\t' && entry[nameEnd] != '\n') {
        nameEnd++;
      }
      if (nameEnd - nameStart == tagNameLen && memcmp(entry.c_str() + nameStart, tagNameBuf, tagNameLen) == 0) {
        stack.erase(stack.begin() + i);
        return;
      }
    }
  }
};

// ============================================================================
// Write helpers
// ============================================================================

static void writeClosingTags(FsFile& out, const std::vector<std::string>& stack) {
  for (int i = static_cast<int>(stack.size()) - 1; i >= 0; i--) {
    const std::string& entry = stack[static_cast<size_t>(i)];
    size_t nameStart = 1;
    size_t nameEnd = nameStart;
    while (nameEnd < entry.size() && entry[nameEnd] != ' ' && entry[nameEnd] != '>' && entry[nameEnd] != '/' &&
           entry[nameEnd] != '\t' && entry[nameEnd] != '\n') {
      nameEnd++;
    }
    out.write(reinterpret_cast<const uint8_t*>("</"), 2);
    out.write(reinterpret_cast<const uint8_t*>(entry.c_str() + nameStart), nameEnd - nameStart);
    out.write(reinterpret_cast<const uint8_t*>(">"), 1);
  }
}

static void writeReopenTags(FsFile& out, const std::vector<std::string>& stack) {
  for (const auto& entry : stack) {
    out.write(reinterpret_cast<const uint8_t*>(entry.c_str()), entry.size());
    if (entry.back() != '>') {
      out.write(reinterpret_cast<const uint8_t*>(">"), 1);
    }
  }
}

static void writeStr(FsFile& out, const std::string& s) {
  if (!s.empty()) out.write(reinterpret_cast<const uint8_t*>(s.c_str()), s.size());
}

static std::string sectionPath(const std::string& dir, const std::string& prefix, int idx,
                               const std::string& suffix) {
  return dir + "/" + prefix + "_" + std::to_string(idx) + suffix;
}

// ============================================================================
// splitByByteOffset — main splitting function
// ============================================================================

SplitResult splitByByteOffset(const std::string& inputPath, const std::string& outputDir,
                              const std::string& filePrefix, const std::string& fileSuffix,
                              const std::string& prologue, const std::string& epilogue, size_t bodyStartOffset,
                              size_t maxBodyEndOffset, size_t maxSectionSize, uint8_t* ioBuf, size_t ioBufSize,
                              bool skipNormalize) {
  SplitResult result;

  FsFile inFile;
  if (!SdMan.openFileForRead("HSPLIT", inputPath, inFile)) return result;

  const size_t fileSize = inFile.size();
  if (bodyStartOffset >= fileSize) {
    inFile.close();
    return result;
  }

  inFile.seek(static_cast<uint32_t>(bodyStartOffset));
  const size_t bodyEndOffset = (maxBodyEndOffset > bodyStartOffset) ? maxBodyEndOffset : fileSize;
  const size_t bodySize = bodyEndOffset - bodyStartOffset;

  // If body fits in one section, create a single section file without tag tracking
  if (bodySize <= maxSectionSize) {
    const std::string outPath = sectionPath(outputDir, filePrefix, 0, fileSuffix);
    FsFile outFile;
    if (!SdMan.openFileForWrite("HSPLIT", outPath, outFile)) {
      inFile.close();
      return result;
    }
    writeStr(outFile, prologue);
    constexpr size_t kBuf = 512;
    uint8_t buf[kBuf];
    size_t remaining = bodySize;
    while (remaining > 0) {
      const size_t n = inFile.read(buf, remaining < kBuf ? remaining : kBuf);
      if (n == 0) break;
      outFile.write(buf, n);
      remaining -= n;
    }
    writeStr(outFile, epilogue);
    outFile.close();
    inFile.close();

    if (!skipNormalize) {
      const std::string normPath = outPath + ".norm";
      if (normalizeHtmlForXml(outPath, normPath)) {
        SdMan.remove(outPath.c_str());
        SdMan.rename(normPath.c_str(), outPath.c_str());
      }
    }
    result.sectionCount = 1;
    return result;
  }

  // Multi-section split with tag tracking
  TagTracker tracker;
  int sectionIdx = 0;
  size_t sectionBytes = 0;
  bool needSplit = false;

  auto openSection = [&]() -> FsFile {
    const std::string path = sectionPath(outputDir, filePrefix, sectionIdx, fileSuffix);
    FsFile f;
    SdMan.openFileForWrite("HSPLIT", path, f);
    if (f.isOpen()) {
      writeStr(f, prologue);
      writeReopenTags(f, tracker.stack);
    }
    return f;
  };

  auto closeSection = [&](FsFile& f) {
    writeClosingTags(f, tracker.stack);
    writeStr(f, epilogue);
    f.close();
    if (!skipNormalize) {
      const std::string path = sectionPath(outputDir, filePrefix, sectionIdx, fileSuffix);
      const std::string normPath = path + ".norm";
      if (normalizeHtmlForXml(path, normPath)) {
        SdMan.remove(path.c_str());
        SdMan.rename(normPath.c_str(), path.c_str());
      }
    }
    sectionIdx++;
  };

  FsFile outFile = openSection();
  if (!outFile.isOpen()) {
    inFile.close();
    return result;
  }

  constexpr size_t kDefaultInBufSize = 512;
  constexpr size_t kWrBufSize = 256;
  const size_t kInBufSize = (ioBuf && ioBufSize > 0) ? ioBufSize : kDefaultInBufSize;
  uint8_t stackInBuf[kDefaultInBufSize];
  uint8_t* inBuf = (ioBuf && ioBufSize > 0) ? ioBuf : stackInBuf;
  uint8_t wrBuf[kWrBufSize];
  size_t wrLen = 0;
  size_t inputRemaining = bodySize;

  auto flushWr = [&]() {
    if (wrLen > 0) {
      outFile.write(wrBuf, wrLen);
      wrLen = 0;
    }
  };

  auto writeByte = [&](uint8_t b) {
    wrBuf[wrLen++] = b;
    if (wrLen >= kWrBufSize) flushWr();
  };

  size_t yieldCounter = 0;
  while (inputRemaining > 0) {
    const size_t toRead = inputRemaining < kInBufSize ? inputRemaining : kInBufSize;
    const size_t n = inFile.read(inBuf, toRead);
    if (n == 0) break;
    inputRemaining -= n;
    if (++yieldCounter % 64 == 0) SPLIT_YIELD();

    for (size_t i = 0; i < n; i++) {
      const uint8_t b = inBuf[i];
      tracker.processByte(b);
      writeByte(b);
      sectionBytes++;

      if (sectionBytes >= maxSectionSize) needSplit = true;

      if (needSplit && tracker.inNormalState() &&
          (b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == '>')) {
        flushWr();
        closeSection(outFile);

        outFile = openSection();
        if (!outFile.isOpen()) {
          inFile.close();
          result.sectionCount = sectionIdx;
          return result;
        }

        sectionBytes = 0;
        needSplit = false;
      }
    }
  }

  // Close final section
  flushWr();
  closeSection(outFile);
  inFile.close();

  result.sectionCount = sectionIdx;
  (void)maxSectionSize;
  return result;
}

}  // namespace html5
