#include "test_utils.h"

#include <EInkDisplay.h>
#include <EpdFontFamily.h>
#include <array>
#include <map>

// Forward declarations to match GfxRenderer.h
class ExternalFont;
class StreamingEpdFont;

// Minimal GfxRenderer implementation for testing streaming font storage/retrieval
// Only includes streaming font methods and lazy resolver - no rendering logic needed
class GfxRenderer {
 public:
  using FontStyleResolver = void (*)(void* ctx, int fontId, int styleIdx);

  explicit GfxRenderer(EInkDisplay&) {}

  void setStreamingFont(int fontId, EpdFontFamily::Style style, StreamingEpdFont* font) {
    int idx = (style == EpdFontFamily::BOLD_ITALIC) ? EpdFontFamily::BOLD : style;
    _streamingFonts[fontId][idx] = font;
  }

  void setStreamingFont(int fontId, StreamingEpdFont* font) { _streamingFonts[fontId][EpdFontFamily::REGULAR] = font; }

  void removeStreamingFont(int fontId) { _streamingFonts.erase(fontId); }

  void setFontStyleResolver(FontStyleResolver resolver, void* ctx) {
    _fontStyleResolver = resolver;
    _fontStyleResolverCtx = ctx;
  }

  void updateFontFamily(int fontId, EpdFontFamily::Style style, const EpdFont* font) {
    (void)fontId;
    (void)style;
    (void)font;
  }

  StreamingEpdFont* getStreamingFont(int fontId, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = _streamingFonts.find(fontId);
    if (it == _streamingFonts.end()) return nullptr;
    int idx = (style == EpdFontFamily::BOLD_ITALIC) ? EpdFontFamily::BOLD : style;
    StreamingEpdFont* sf = it->second[idx];
    if (!sf && idx != EpdFontFamily::REGULAR && _fontStyleResolver) {
      _fontStyleResolver(_fontStyleResolverCtx, fontId, idx);
      sf = it->second[idx];
    }
    return sf ? sf : it->second[EpdFontFamily::REGULAR];
  }

 private:
  mutable std::map<int, std::array<StreamingEpdFont*, 3>> _streamingFonts;
  FontStyleResolver _fontStyleResolver = nullptr;
  void* _fontStyleResolverCtx = nullptr;
};

// Helper for lazy resolver tests
struct ResolverContext {
  GfxRenderer* gfx;
  StreamingEpdFont* fontToSet;
  int callCount = 0;
  int lastFontId = 0;
  int lastStyleIdx = 0;
};

static void testResolver(void* ctx, int fontId, int styleIdx) {
  auto* rc = static_cast<ResolverContext*>(ctx);
  rc->callCount++;
  rc->lastFontId = fontId;
  rc->lastStyleIdx = styleIdx;
  if (rc->gfx && rc->fontToSet) {
    rc->gfx->setStreamingFont(fontId, static_cast<EpdFontFamily::Style>(styleIdx), rc->fontToSet);
  }
}

static void noopResolver(void* ctx, int fontId, int styleIdx) {
  auto* rc = static_cast<ResolverContext*>(ctx);
  rc->callCount++;
  rc->lastFontId = fontId;
  rc->lastStyleIdx = styleIdx;
  // Intentionally does NOT set any font
}

int main() {
  TestUtils::TestRunner runner("GfxRendererStreamingFont");

  // Create mock display
  EInkDisplay display(0, 0, 0, 0, 0, 0);

  // Use fake pointers for StreamingEpdFont - we only test storage/retrieval, not actual font operations
  auto* regularFont = reinterpret_cast<StreamingEpdFont*>(0x1000);
  auto* boldFont = reinterpret_cast<StreamingEpdFont*>(0x2000);
  auto* italicFont = reinterpret_cast<StreamingEpdFont*>(0x3000);

  // Test 1: setStreamingFont with REGULAR style stores at index 0
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);
    runner.expectEq(regularFont, gfx.getStreamingFont(1, EpdFontFamily::REGULAR),
                    "setStreamingFont_with_style_stores_regular");
  }

  // Test 2: setStreamingFont with BOLD style stores at index 1
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::BOLD, boldFont);
    runner.expectEq(boldFont, gfx.getStreamingFont(1, EpdFontFamily::BOLD), "setStreamingFont_with_style_stores_bold");
  }

  // Test 3: setStreamingFont with ITALIC style stores at index 2
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::ITALIC, italicFont);
    runner.expectEq(italicFont, gfx.getStreamingFont(1, EpdFontFamily::ITALIC),
                    "setStreamingFont_with_style_stores_italic");
  }

  // Test 4: setStreamingFont with BOLD_ITALIC maps to BOLD (index 1)
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::BOLD_ITALIC, boldFont);
    // BOLD_ITALIC should store at BOLD index
    runner.expectEq(boldFont, gfx.getStreamingFont(1, EpdFontFamily::BOLD),
                    "setStreamingFont_bold_italic_maps_to_bold");
  }

  // Test 5: setStreamingFont without style defaults to REGULAR
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, regularFont);
    runner.expectEq(regularFont, gfx.getStreamingFont(1, EpdFontFamily::REGULAR),
                    "setStreamingFont_without_style_defaults_to_regular");
  }

  // Test 6: getStreamingFont returns correct style when all styles set
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);
    gfx.setStreamingFont(1, EpdFontFamily::BOLD, boldFont);
    gfx.setStreamingFont(1, EpdFontFamily::ITALIC, italicFont);

    runner.expectEq(regularFont, gfx.getStreamingFont(1, EpdFontFamily::REGULAR),
                    "getStreamingFont_returns_correct_style_regular");
    runner.expectEq(boldFont, gfx.getStreamingFont(1, EpdFontFamily::BOLD),
                    "getStreamingFont_returns_correct_style_bold");
    runner.expectEq(italicFont, gfx.getStreamingFont(1, EpdFontFamily::ITALIC),
                    "getStreamingFont_returns_correct_style_italic");
  }

  // Test 7: getStreamingFont with BOLD_ITALIC returns BOLD font
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);
    gfx.setStreamingFont(1, EpdFontFamily::BOLD, boldFont);

    runner.expectEq(boldFont, gfx.getStreamingFont(1, EpdFontFamily::BOLD_ITALIC),
                    "getStreamingFont_bold_italic_returns_bold");
  }

  // Test 8: getStreamingFont falls back to REGULAR when requested style is missing
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);
    // BOLD and ITALIC not set

    runner.expectEq(regularFont, gfx.getStreamingFont(1, EpdFontFamily::BOLD),
                    "getStreamingFont_missing_bold_fallback_to_regular");
    runner.expectEq(regularFont, gfx.getStreamingFont(1, EpdFontFamily::ITALIC),
                    "getStreamingFont_missing_italic_fallback_to_regular");
  }

  // Test 9: getStreamingFont returns nullptr for nonexistent fontId
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, regularFont);

    runner.expectEq(static_cast<StreamingEpdFont*>(nullptr), gfx.getStreamingFont(999),
                    "getStreamingFont_nonexistent_fontid_returns_nullptr");
  }

  // Test 10: removeStreamingFont clears all styles for the fontId
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);
    gfx.setStreamingFont(1, EpdFontFamily::BOLD, boldFont);
    gfx.setStreamingFont(1, EpdFontFamily::ITALIC, italicFont);

    gfx.removeStreamingFont(1);

    runner.expectEq(static_cast<StreamingEpdFont*>(nullptr), gfx.getStreamingFont(1, EpdFontFamily::REGULAR),
                    "removeStreamingFont_clears_regular");
    runner.expectEq(static_cast<StreamingEpdFont*>(nullptr), gfx.getStreamingFont(1, EpdFontFamily::BOLD),
                    "removeStreamingFont_clears_bold");
    runner.expectEq(static_cast<StreamingEpdFont*>(nullptr), gfx.getStreamingFont(1, EpdFontFamily::ITALIC),
                    "removeStreamingFont_clears_italic");
  }

  // Test 11: Multiple fontIds are independent
  {
    GfxRenderer gfx(display);
    auto* font1Regular = reinterpret_cast<StreamingEpdFont*>(0x1001);
    auto* font1Bold = reinterpret_cast<StreamingEpdFont*>(0x1002);
    auto* font2Regular = reinterpret_cast<StreamingEpdFont*>(0x2001);
    auto* font2Italic = reinterpret_cast<StreamingEpdFont*>(0x2003);

    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, font1Regular);
    gfx.setStreamingFont(1, EpdFontFamily::BOLD, font1Bold);
    gfx.setStreamingFont(2, EpdFontFamily::REGULAR, font2Regular);
    gfx.setStreamingFont(2, EpdFontFamily::ITALIC, font2Italic);

    // Verify fontId 1
    runner.expectEq(font1Regular, gfx.getStreamingFont(1, EpdFontFamily::REGULAR),
                    "multiple_fontids_independent_font1_regular");
    runner.expectEq(font1Bold, gfx.getStreamingFont(1, EpdFontFamily::BOLD),
                    "multiple_fontids_independent_font1_bold");
    // fontId 1 has no ITALIC, should fall back to REGULAR
    runner.expectEq(font1Regular, gfx.getStreamingFont(1, EpdFontFamily::ITALIC),
                    "multiple_fontids_independent_font1_italic_fallback");

    // Verify fontId 2
    runner.expectEq(font2Regular, gfx.getStreamingFont(2, EpdFontFamily::REGULAR),
                    "multiple_fontids_independent_font2_regular");
    runner.expectEq(font2Italic, gfx.getStreamingFont(2, EpdFontFamily::ITALIC),
                    "multiple_fontids_independent_font2_italic");
    // fontId 2 has no BOLD, should fall back to REGULAR
    runner.expectEq(font2Regular, gfx.getStreamingFont(2, EpdFontFamily::BOLD),
                    "multiple_fontids_independent_font2_bold_fallback");

    // Remove fontId 1, verify fontId 2 unaffected
    gfx.removeStreamingFont(1);
    runner.expectEq(static_cast<StreamingEpdFont*>(nullptr), gfx.getStreamingFont(1),
                    "multiple_fontids_independent_font1_removed");
    runner.expectEq(font2Regular, gfx.getStreamingFont(2, EpdFontFamily::REGULAR),
                    "multiple_fontids_independent_font2_unaffected");
  }

  // ============================================
  // Lazy Font Resolver Tests
  // ============================================

  // Test 12: Resolver called when bold is null - provides bold font
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);

    ResolverContext ctx = {&gfx, boldFont, 0, 0, 0};
    gfx.setFontStyleResolver(testResolver, &ctx);

    StreamingEpdFont* result = gfx.getStreamingFont(1, EpdFontFamily::BOLD);
    runner.expectEq(boldFont, result, "resolver_called_when_bold_is_null: returns bold from resolver");
    runner.expectEq(1, ctx.callCount, "resolver_called_when_bold_is_null: resolver called once");
    runner.expectEq(1, ctx.lastFontId, "resolver_called_when_bold_is_null: correct fontId");
    runner.expectEq(static_cast<int>(EpdFontFamily::BOLD), ctx.lastStyleIdx,
                    "resolver_called_when_bold_is_null: correct styleIdx");
  }

  // Test 13: Resolver called when italic is null - provides italic font
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);

    ResolverContext ctx = {&gfx, italicFont, 0, 0, 0};
    gfx.setFontStyleResolver(testResolver, &ctx);

    StreamingEpdFont* result = gfx.getStreamingFont(1, EpdFontFamily::ITALIC);
    runner.expectEq(italicFont, result, "resolver_called_when_italic_is_null: returns italic from resolver");
    runner.expectEq(1, ctx.callCount, "resolver_called_when_italic_is_null: resolver called once");
    runner.expectEq(static_cast<int>(EpdFontFamily::ITALIC), ctx.lastStyleIdx,
                    "resolver_called_when_italic_is_null: correct styleIdx");
  }

  // Test 14: Resolver NOT called when requested style already exists
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);
    gfx.setStreamingFont(1, EpdFontFamily::BOLD, boldFont);

    ResolverContext ctx = {&gfx, nullptr, 0, 0, 0};
    gfx.setFontStyleResolver(testResolver, &ctx);

    StreamingEpdFont* result = gfx.getStreamingFont(1, EpdFontFamily::BOLD);
    runner.expectEq(boldFont, result, "resolver_not_called_when_style_exists: returns existing bold");
    runner.expectEq(0, ctx.callCount, "resolver_not_called_when_style_exists: resolver not called");
  }

  // Test 15: Resolver NOT called for REGULAR style
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);

    ResolverContext ctx = {&gfx, boldFont, 0, 0, 0};
    gfx.setFontStyleResolver(testResolver, &ctx);

    StreamingEpdFont* result = gfx.getStreamingFont(1, EpdFontFamily::REGULAR);
    runner.expectEq(regularFont, result, "resolver_not_called_for_regular: returns regular");
    runner.expectEq(0, ctx.callCount, "resolver_not_called_for_regular: resolver not called");
  }

  // Test 16: Resolver fails (doesn't set font) - falls back to regular
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);

    ResolverContext ctx = {&gfx, nullptr, 0, 0, 0};
    gfx.setFontStyleResolver(noopResolver, &ctx);

    StreamingEpdFont* result = gfx.getStreamingFont(1, EpdFontFamily::BOLD);
    runner.expectEq(regularFont, result,
                    "resolver_fallback_to_regular_when_fails: falls back to regular");
    runner.expectEq(1, ctx.callCount, "resolver_fallback_to_regular_when_fails: resolver was called");
  }

  // Test 17: Resolver called once, second access uses cached result
  {
    GfxRenderer gfx(display);
    gfx.setStreamingFont(1, EpdFontFamily::REGULAR, regularFont);

    ResolverContext ctx = {&gfx, boldFont, 0, 0, 0};
    gfx.setFontStyleResolver(testResolver, &ctx);

    StreamingEpdFont* result1 = gfx.getStreamingFont(1, EpdFontFamily::BOLD);
    StreamingEpdFont* result2 = gfx.getStreamingFont(1, EpdFontFamily::BOLD);
    runner.expectEq(boldFont, result1, "resolver_called_once_then_cached: first call returns bold");
    runner.expectEq(boldFont, result2, "resolver_called_once_then_cached: second call returns bold");
    runner.expectEq(1, ctx.callCount, "resolver_called_once_then_cached: resolver called exactly once");
  }

  return runner.allPassed() ? 0 : 1;
}
