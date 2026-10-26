// CssParser class tests - exercises the actual CssParser implementation
// with mock SDCardManager to test safety limits (rule count, selector
// length, file size).

#include "test_utils.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <CssParser.h>
#include <esp_heap_caps.h>
#include <uzlib.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

#include <string>

namespace {
bool fragmentedHeap = false;
size_t availableBytes[] = {0, 60000, 30000};
struct alignas(std::max_align_t) AllocationHeader {
  size_t bytes;
  size_t pool;
};

void updateHeap() {
  testSetHeapPool(MALLOC_CAP_INTERNAL, availableBytes[1] + availableBytes[2],
                  std::max(std::min(availableBytes[1], size_t{3000}), availableBytes[2]));
  testSetHeapPool(MALLOC_CAP_SPIRAM, 0, 0);
}

void* allocate(size_t size) {
  const size_t bytes = std::max(size, size_t{1}) + sizeof(AllocationHeader);
  const size_t pool = !fragmentedHeap ? 0 : bytes <= 3000 && bytes <= availableBytes[1] ? 1 : 2;
  if (pool && bytes > availableBytes[pool]) return nullptr;
  auto* header = static_cast<AllocationHeader*>(std::malloc(bytes));
  if (!header) return nullptr;
  *header = {bytes, pool};
  if (pool) {
    availableBytes[pool] -= bytes;
    updateHeap();
  }
  return header + 1;
}

void release(void* pointer) {
  if (!pointer) return;
  auto* header = static_cast<AllocationHeader*>(pointer) - 1;
  if (header->pool) {
    availableBytes[header->pool] += header->bytes;
    if (fragmentedHeap) updateHeap();
  }
  std::free(header);
}
}  // namespace

void* operator new(size_t size) {
  void* pointer = allocate(size);
  if (!pointer) throw std::bad_alloc();
  return pointer;
}
void* operator new[](size_t size) { return ::operator new(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, size_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }

int main() {
  TestUtils::TestRunner runner("CssParser Class");

  // ============================================
  // Basic parseFile() with mock SDCardManager
  // ============================================

  // Test 1: Parse a simple CSS file via mock
  {
    SdMan.clearFiles();
    SdMan.registerFile("/test.css", "p { text-align: center; }");

    CssParser parser;
    bool ok = parser.parseFile("/test.css");
    runner.expectTrue(ok, "parseFile: simple CSS succeeds");
    runner.expectTrue(parser.hasStyles(), "parseFile: has styles");

    CssStyle style = parser.getTagStyle("p");
    runner.expectTrue(style.hasTextAlign(), "parseFile: p has text-align");
    runner.expectTrue(style.textAlign == TextAlign::Center, "parseFile: p text-align is center");
  }

  // Test 2: Parse multiple rules
  {
    SdMan.clearFiles();
    SdMan.registerFile("/multi.css",
                       "p { text-align: left; }\n"
                       ".bold { font-weight: bold; }\n"
                       "h1 { font-weight: bold; text-align: center; }\n");

    CssParser parser;
    bool ok = parser.parseFile("/multi.css");
    runner.expectTrue(ok, "parseFile multi: succeeds");
    runner.expectEq(static_cast<size_t>(3), parser.getStyleCount(), "parseFile multi: 3 rules");

    CssStyle h1 = parser.getTagStyle("h1");
    runner.expectTrue(h1.hasFontWeight() && h1.fontWeight == CssFontWeight::Bold, "parseFile multi: h1 bold");
    runner.expectTrue(h1.hasTextAlign() && h1.textAlign == TextAlign::Center, "parseFile multi: h1 center");
  }

  // Test 3: getCombinedStyle merges tag + class
  {
    SdMan.clearFiles();
    SdMan.registerFile("/combined.css",
                       "p { text-align: left; }\n"
                       ".italic { font-style: italic; }\n"
                       "p.special { font-weight: bold; }\n");

    CssParser parser;
    parser.parseFile("/combined.css");

    CssStyle style = parser.getCombinedStyle("p", "italic special");
    runner.expectTrue(style.hasTextAlign(), "getCombinedStyle: has tag style");
    runner.expectTrue(style.hasFontStyle() && style.fontStyle == CssFontStyle::Italic,
                      "getCombinedStyle: has class style");
    runner.expectTrue(style.hasFontWeight() && style.fontWeight == CssFontWeight::Bold,
                      "getCombinedStyle: has tag.class style");
  }

  // Test 4: Nonexistent file returns false
  {
    SdMan.clearFiles();
    CssParser parser;
    bool ok = parser.parseFile("/nonexistent.css");
    runner.expectFalse(ok, "parseFile: nonexistent file fails");
    runner.expectFalse(parser.hasStyles(), "parseFile: no styles from missing file");
  }

  // ============================================
  // File size limit (MAX_CSS_FILE_SIZE = 64KB)
  // ============================================

  // Test 5: File exactly at limit parses OK
  {
    SdMan.clearFiles();
    // 64KB = 65536 bytes. Create a valid CSS string of exactly that size.
    std::string css = "p { text-align: center; }";
    css.resize(64 * 1024, ' ');  // pad with spaces to exactly 64KB
    SdMan.registerFile("/at_limit.css", css);

    CssParser parser;
    bool ok = parser.parseFile("/at_limit.css");
    runner.expectTrue(ok, "file size: 64KB file accepted");
  }

  // Test 6: File over limit is rejected
  {
    SdMan.clearFiles();
    std::string css = "p { text-align: center; }";
    css.resize(64 * 1024 + 1, ' ');  // one byte over limit
    SdMan.registerFile("/over_limit.css", css);

    CssParser parser;
    bool ok = parser.parseFile("/over_limit.css");
    runner.expectFalse(ok, "file size: 64KB+1 file rejected");
    runner.expectFalse(parser.hasStyles(), "file size: no styles from oversized file");
  }

  // ============================================
  // Rule count limit (MAX_CSS_RULES = 512)
  // ============================================

  // Test 7: Exactly 512 rules are stored
  {
    SdMan.clearFiles();
    std::string css;
    for (int i = 0; i < 512; i++) {
      css += ".cls" + std::to_string(i) + " { text-align: center; }\n";
    }
    SdMan.registerFile("/at_rule_limit.css", css);

    CssParser parser;
    bool ok = parser.parseFile("/at_rule_limit.css");
    runner.expectTrue(ok, "rule limit: 512 rules parses OK");
    runner.expectEq(static_cast<size_t>(512), parser.getStyleCount(), "rule limit: all 512 stored");
  }

  // Test 8: Rules beyond 512 are dropped
  {
    SdMan.clearFiles();
    std::string css;
    for (int i = 0; i < 600; i++) {
      css += ".cls" + std::to_string(i) + " { text-align: center; }\n";
    }
    SdMan.registerFile("/over_rule_limit.css", css);

    CssParser parser;
    bool ok = parser.parseFile("/over_rule_limit.css");
    runner.expectTrue(ok, "rule limit: 600 rules parses OK (no error)");
    runner.expectEq(static_cast<size_t>(512), parser.getStyleCount(), "rule limit: capped at 512");
  }

  // Test 9: Existing rules can still be merged when at limit
  {
    SdMan.clearFiles();
    std::string css;
    // Fill to limit with unique selectors
    for (int i = 0; i < 512; i++) {
      css += ".cls" + std::to_string(i) + " { text-align: left; }\n";
    }
    // Add a rule for an existing selector - should merge, not be dropped
    css += ".cls0 { font-weight: bold; }\n";
    // Add a new selector that should be dropped
    css += ".new_rule { text-align: right; }\n";
    SdMan.registerFile("/merge_at_limit.css", css);

    CssParser parser;
    parser.parseFile("/merge_at_limit.css");

    runner.expectEq(static_cast<size_t>(512), parser.getStyleCount(), "merge at limit: still 512 rules");

    // .cls0 should have merged properties
    CssStyle style = parser.getCombinedStyle("div", "cls0");
    runner.expectTrue(style.hasTextAlign(), "merge at limit: cls0 has text-align");
    runner.expectTrue(style.hasFontWeight() && style.fontWeight == CssFontWeight::Bold,
                      "merge at limit: cls0 merged bold");

    // .new_rule should not exist
    const CssStyle* newStyle = parser.getStyleForClass(".new_rule");
    runner.expectTrue(newStyle == nullptr, "merge at limit: new rule was dropped");
  }

  // ============================================
  // Selector length limit (MAX_CSS_SELECTOR_LENGTH = 256)
  // ============================================

  // Test 10: Long selector is truncated but doesn't crash
  {
    SdMan.clearFiles();
    // Create a selector longer than 256 chars
    std::string longSelector(300, 'x');
    std::string css = "." + longSelector + " { text-align: center; }\n";
    css += "p { font-weight: bold; }\n";
    SdMan.registerFile("/long_selector.css", css);

    CssParser parser;
    bool ok = parser.parseFile("/long_selector.css");
    runner.expectTrue(ok, "long selector: parses without crash");
    // The p rule should still be parsed correctly
    CssStyle pStyle = parser.getTagStyle("p");
    runner.expectTrue(pStyle.hasFontWeight() && pStyle.fontWeight == CssFontWeight::Bold,
                      "long selector: subsequent rules still parsed");
  }

  // Test 11: Selector exactly at limit works
  {
    SdMan.clearFiles();
    // Selector with class dot + 255 chars = 256 total
    std::string selector(255, 'a');
    std::string css = "." + selector + " { text-align: center; }\n";
    SdMan.registerFile("/exact_selector.css", css);

    CssParser parser;
    parser.parseFile("/exact_selector.css");
    runner.expectTrue(parser.hasStyles(), "exact selector: style stored");
  }

  // ============================================
  // Comments and AT-rules still work
  // ============================================

  // Test 12: CSS comments are ignored
  {
    SdMan.clearFiles();
    SdMan.registerFile("/comments.css",
                       "/* comment */ p { text-align: center; } /* another */\n"
                       "h1 { /* inline comment */ font-weight: bold; }\n");

    CssParser parser;
    parser.parseFile("/comments.css");
    runner.expectEq(static_cast<size_t>(2), parser.getStyleCount(), "comments: 2 rules parsed");
  }

  // Test 13: @-rules are skipped
  {
    SdMan.clearFiles();
    SdMan.registerFile("/at_rules.css",
                       "@charset \"UTF-8\";\n"
                       "@import url('other.css');\n"
                       "@media screen { .mobile { text-align: left; } }\n"
                       "p { text-align: center; }\n");

    CssParser parser;
    parser.parseFile("/at_rules.css");
    // Only the p rule should be parsed (media block content is skipped)
    CssStyle style = parser.getTagStyle("p");
    runner.expectTrue(style.hasTextAlign() && style.textAlign == TextAlign::Center,
                      "at-rules: p rule parsed after @-rules");
  }

  // Test 14: Comma-separated selectors create multiple rules
  {
    SdMan.clearFiles();
    SdMan.registerFile("/comma.css", "h1, h2, h3 { font-weight: bold; }\n");

    CssParser parser;
    parser.parseFile("/comma.css");
    runner.expectEq(static_cast<size_t>(3), parser.getStyleCount(), "comma selectors: 3 rules from 1 declaration");

    CssStyle h2 = parser.getTagStyle("h2");
    runner.expectTrue(h2.hasFontWeight() && h2.fontWeight == CssFontWeight::Bold, "comma selectors: h2 is bold");
  }

  // Test 15: clear() resets state
  {
    SdMan.clearFiles();
    SdMan.registerFile("/clear.css", "p { text-align: center; }");

    CssParser parser;
    parser.parseFile("/clear.css");
    runner.expectTrue(parser.hasStyles(), "clear: has styles before clear");
    parser.clear();
    runner.expectFalse(parser.hasStyles(), "clear: no styles after clear");
    runner.expectEq(static_cast<size_t>(0), parser.getStyleCount(), "clear: count is 0");
  }

  // Test 16: parseInlineStyle (static method)
  {
    CssStyle style = CssParser::parseInlineStyle("text-align: right; font-style: italic");
    runner.expectTrue(style.hasTextAlign() && style.textAlign == TextAlign::Right,
                      "parseInlineStyle: text-align right");
    runner.expectTrue(style.hasFontStyle() && style.fontStyle == CssFontStyle::Italic,
                      "parseInlineStyle: font-style italic");
  }

  // ============================================
  // Selector filtering: combinators and complex selectors are skipped
  // ============================================
  // The matcher only supports tag, .class, and tag.class selectors. Anything
  // containing combinators (+ > ~), descendant whitespace, attribute ([),
  // pseudo (:), id (#), or universal (*) is skipped at parseRule() time so
  // it doesn't fill MAX_CSS_RULES with rules that can never match.

  // Test 17: combinator/complex selectors are dropped, simple ones kept
  {
    SdMan.clearFiles();
    SdMan.registerFile("/combinators.css",
                       "p { text-align: center; }\n"
                       ".note { font-style: italic; }\n"
                       "div > p { font-weight: bold; }\n"
                       "div + p { font-weight: bold; }\n"
                       "div ~ p { font-weight: bold; }\n"
                       "div p { font-weight: bold; }\n"
                       "* { font-weight: bold; }\n"
                       "#id { font-weight: bold; }\n"
                       "a:hover { font-weight: bold; }\n"
                       "[type=text] { font-weight: bold; }\n");

    CssParser parser;
    bool ok = parser.parseFile("/combinators.css");
    runner.expectTrue(ok, "selector filter: parseFile succeeds");
    runner.expectEq(static_cast<size_t>(2), parser.getStyleCount(), "selector filter: only p and .note kept");

    CssStyle p = parser.getTagStyle("p");
    runner.expectTrue(p.hasTextAlign() && p.textAlign == TextAlign::Center,
                      "selector filter: simple tag rule preserved");
    runner.expectFalse(p.hasFontWeight(), "selector filter: combinator rules did not bleed into p");

    const CssStyle* note = parser.getStyleForClass(".note");
    runner.expectTrue(note != nullptr && note->hasFontStyle() && note->fontStyle == CssFontStyle::Italic,
                      "selector filter: simple class rule preserved");
  }

  // Test 18: comma-separated list keeps supported parts, drops unsupported parts
  {
    SdMan.clearFiles();
    SdMan.registerFile("/mixed_comma.css", "p, div > span, .keep, a:hover { text-align: right; }\n");

    CssParser parser;
    parser.parseFile("/mixed_comma.css");
    // Only "p" and ".keep" should survive; "div > span" and "a:hover" are dropped.
    runner.expectEq(static_cast<size_t>(2), parser.getStyleCount(), "selector filter: comma list keeps 2 of 4");

    CssStyle p = parser.getTagStyle("p");
    runner.expectTrue(p.hasTextAlign() && p.textAlign == TextAlign::Right,
                      "selector filter: p kept from comma list");
    const CssStyle* keep = parser.getStyleForClass(".keep");
    runner.expectTrue(keep != nullptr && keep->hasTextAlign() && keep->textAlign == TextAlign::Right,
                      "selector filter: .keep kept from comma list");
  }

  {
    SdMan.reset();
    SdMan.registerFile("/styles.css",
                       "p { text-align: center; margin: 2em 4pt 3rem 7px; padding-right: 25%; direction: rtl; }"
                       ".note { margin-left: 0; font-style: italic; } p.note { font-weight: bold; }");
    CssParser original;
    runner.expectTrue(original.parseFile("/styles.css"), "cache: source parses");
    runner.expectTrue(original.saveCache("/styles.bin", 1234), "cache: complete rules saved");
    CssParser restored;
    runner.expectTrue(restored.loadCache("/styles.bin", 1234), "cache: complete rules restored");
    const auto style = restored.getCombinedStyle("p", "note");
    runner.expectTrue(style.hasTextAlign() && style.textAlign == TextAlign::Center, "cache: tag alignment");
    runner.expectTrue(style.hasMarginLeft() && style.marginLeft.value == 0, "cache: explicit zero overrides");
    runner.expectTrue(style.marginTop.value == 2 && style.marginTop.unit == CssUnit::Em, "cache: em preserved");
    runner.expectTrue(style.marginBottom.value == 3 && style.marginBottom.unit == CssUnit::Rem,
                      "cache: rem preserved");
    runner.expectTrue(style.marginRight.value == 4 && style.marginRight.unit == CssUnit::Points,
                      "cache: points preserved");
    runner.expectTrue(restored.getTagStyle("p").marginLeft.value == 7 &&
                          restored.getTagStyle("p").marginLeft.unit == CssUnit::Pixels,
                      "cache: pixels preserved");
    runner.expectTrue(style.paddingRight.value == 25 && style.paddingRight.unit == CssUnit::Percent,
                      "cache: percent preserved");
    runner.expectTrue(style.fontStyle == CssFontStyle::Italic && style.fontWeight == CssFontWeight::Bold &&
                          style.direction == TextDirection::Rtl,
                      "cache: class and tag-class cascade");
    runner.expectTrue(style.marginTop.toPixels(12) == 24 && style.marginTop.toPixels(24) == 48 &&
                          style.paddingRight.toPixels(24, 400) == 100,
                      "cache: font and viewport changes retain relative layout");
    runner.expectFalse(restored.loadCache("/styles.bin", 5678), "cache: reject changed source");
    runner.expectFalse(restored.hasStyles(), "cache: stale rules cleared");
    runner.expectFalse(original.saveCache("/unknown.bin", 0), "cache: unknown source cannot persist");
    runner.expectFalse(SdMan.exists("/unknown.bin"), "cache: no unkeyed cache published");
  }

  {
    SdMan.reset();
    CssParser empty;
    runner.expectTrue(empty.saveCache("/empty.bin", 1234), "cache: empty result saved");
    SdMan.registerFile("/old.css", "p { text-align: right; }");
    CssParser restored;
    restored.parseFile("/old.css");
    runner.expectTrue(restored.loadCache("/empty.bin", 1234), "cache: empty result restored");
    runner.expectFalse(restored.getTagStyle("p").hasTextAlign(), "cache: empty result removes old styles");
  }

  {
    SdMan.reset();
    SdMan.registerFile("/one.css", "p { text-align: center; }");
    CssParser original;
    original.parseFile("/one.css");
    original.saveCache("/one.bin", 1234);
    const std::string valid = SdMan.getWrittenData("/one.bin");
    auto seal = [](std::string& bytes) {
      const uint32_t crc = uzlib_crc32(bytes.data(), bytes.size() - 4, 0xffffffff) ^ 0xffffffff;
      std::memcpy(bytes.data() + bytes.size() - 4, &crc, sizeof(crc));
    };
    auto rejected = [&](const std::string& bytes, const char* reason) {
      SdMan.registerFile("/bad.bin", bytes);
      CssParser restored;
      runner.expectFalse(restored.loadCache("/bad.bin", 1234), reason);
      runner.expectFalse(restored.hasStyles(), "cache: invalid load leaves no partial rules");
    };
    if (valid.size() == 67) {
      for (const auto& change : {std::pair<size_t, uint8_t>{4, 2}, {10, 3}, {12, 1}, {17, 128},
                                 {18, 255}, {27, 255}}) {
        auto bytes = valid;
        bytes[change.first] = static_cast<char>(change.second);
        seal(bytes);
        rejected(bytes, "cache: reject invalid version, count, selector, flags, enum, or unit");
      }
      auto nonfinite = valid;
      const uint32_t nanBits = 0x7fc00000;
      std::memcpy(nonfinite.data() + 23, &nanBits, sizeof(nanBits));
      seal(nonfinite);
      rejected(nonfinite, "cache: reject non-finite length");
      auto duplicate = valid.substr(0, 63) + valid.substr(11, 52) + std::string(4, '\0');
      duplicate[9] = 2;
      seal(duplicate);
      rejected(duplicate, "cache: reject duplicate selector");
      auto corrupt = valid;
      corrupt[18] ^= 1;
      rejected(corrupt, "cache: reject changed payload");
      rejected(valid + "extra", "cache: reject trailing data");
      for (size_t length : {size_t{0}, size_t{10}, valid.size() - 1}) {
        rejected(valid.substr(0, length), "cache: reject truncated data");
      }
    } else {
      runner.expectTrue(false, "cache: fixture contains one complete wire record");
    }
    CssParser cancelled;
    runner.expectFalse(cancelled.loadCache("/one.bin", 1234, [] { return true; }),
                       "cache: cancelled load fails");
    runner.expectFalse(cancelled.hasStyles(), "cache: cancelled load has no partial rules");
    testSetLargestFreeBlock(0);
    runner.expectFalse(cancelled.loadCache("/one.bin", 1234), "cache: rule allocation needs heap");
    testResetLargestFreeBlock();
  }

  for (int failure = 0; failure < 5; ++failure) {
    SdMan.reset();
    SdMan.registerFile("/old.css", "p { text-align: left; }");
    SdMan.registerFile("/new.css", "p { text-align: right; }");
    CssParser original;
    original.parseFile("/old.css");
    original.saveCache("/replace.bin", 1234);
    const auto originalBytes = SdMan.getWrittenData("/replace.bin");
    CssParser replacement;
    replacement.parseFile("/new.css");
    if (failure == 0) SdMan.setWriteLimit(20);
    if (failure == 1) SdMan.setSyncResult(false);
    if (failure == 2) SdMan.setRenameResult(false);
    if (failure == 4) SdMan.setCloseResult(false);
    const auto abort = [&] {
      return failure == 3 && SdMan.getWrittenData("/replace.bin.tmp").size() > 11;
    };
    runner.expectFalse(replacement.saveCache("/replace.bin", 1234, abort), "cache: interrupted save fails");
    runner.expectFalse(SdMan.exists("/replace.bin.tmp"), "cache: temporary file removed");
    CssParser restored;
    if (failure == 2) {
      runner.expectFalse(restored.loadCache("/replace.bin", 1234), "cache: failed rename leaves cache missing");
    } else {
      runner.expectTrue(restored.loadCache("/replace.bin", 1234), "cache: pre-publication failure retains cache");
      runner.expectEqual(originalBytes, SdMan.getWrittenData("/replace.bin"), "cache: old bytes retained");
      runner.expectTrue(restored.getTagStyle("p").textAlign == TextAlign::Left,
                        "cache: old complete styles retained");
    }
  }

  {
    SdMan.reset();
    SdMan.registerFile("/interrupted.css", "p { text-align: left; } q { text-align: right; }");
    CssParser parser;
    SdMan.setReadLimit(30);
    runner.expectFalse(parser.parseFile("/interrupted.css"), "read: short input fails instead of accepting a prefix");
    runner.expectFalse(parser.hasStyles(), "read: partial stylesheet discarded");
    SdMan.setReadLimit(SIZE_MAX);
    runner.expectFalse(parser.parseFile("/interrupted.css", [&] { return parser.hasStyles(); }),
                       "abort: parsing stops after completed rules");
    runner.expectFalse(parser.hasStyles(), "abort: partial stylesheet discarded");
  }

  {
    SdMan.reset();
    SdMan.registerFile("/low-heap.css", "p { text-align: center; margin-top: 2em; }");
    testSetLargestFreeBlock(0);
    CssParser parser;
    runner.expectFalse(parser.parseFile("/low-heap.css"), "memory: reject rules without allocation space");
    runner.expectFalse(parser.hasStyles(), "memory: no partial rules after failure");
    testResetLargestFreeBlock();
  }

  for (bool styled : {false, true}) {
    SdMan.reset();
    SdMan.registerFile("/padded.css", "p {" + std::string(10000, ' ') +
                                         (styled ? "text-align: center" : "") + std::string(10000, ' ') + "}");
    testSetHeapPool(MALLOC_CAP_INTERNAL, 94196, 94196);
    testSetHeapPool(MALLOC_CAP_SPIRAM, 0, 0);
    CssParser parser;
    runner.expectTrue(parser.parseFile("/padded.css"), "memory: whitespace does not require declaration copies");
    runner.expectTrue(styled ? parser.getTagStyle("p").textAlign == TextAlign::Center : !parser.hasStyles(),
                      "memory: padded declarations keep their meaning");
    testResetLargestFreeBlock();
  }

  {
    SdMan.reset();
    SdMan.registerFile("/seed.css", "p { text-align: center; }");
    SdMan.registerFile("/fragmented.css", "q { font-family: " + std::string(5000, 'a') + "; }");
    CssParser parser;
    parser.parseFile("/seed.css");
    fragmentedHeap = true;
    updateHeap();
    bool parsed = false;
    bool threw = false;
    try {
      parsed = parser.parseFile("/fragmented.css");
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    fragmentedHeap = false;
    testResetLargestFreeBlock();
    runner.expectFalse(threw, "memory: fragmented heap fails before an allocation throws");
    runner.expectFalse(parsed, "memory: insufficient contiguous space rejects the stylesheet");
    runner.expectFalse(parser.hasStyles(), "memory: fragmented heap failure clears partial styles");
  }

  SdMan.clearFiles();
  return runner.allPassed() ? 0 : 1;
}
