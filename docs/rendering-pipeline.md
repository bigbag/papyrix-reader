# Rendering Pipeline Memory Usage (Reader Mode)

This document describes RAM usage in **Reader mode** across anti-aliasing (AA) and font type combinations. Reader mode is the minimal-footprint mode optimized for reading, and it always uses the full page viewport.

## Display Framebuffer

The device uses single-buffer mode (`-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1` in `platformio.ini`).

One statically-allocated framebuffer, sized for the larger panel:

- **X4:** 800 × 480 — 100 bytes/row × 480 = 48,000 bytes
- **X3:** 792 × 528 — 99 bytes/row × 528 = 52,272 bytes

The static buffer is allocated at `MAX_BUFFER_SIZE = 52,272` bytes to support both panels (`lib/EInkDisplay/include/EInkDisplay.h`). This is always present — it cannot be freed.

## Viewport Dimensions

Base margins from `GfxRenderer` (`lib/GfxRenderer/src/GfxRenderer.h`):

- **Top:** base 9 → effective 9
- **Left:** base 3 + 5 padding → effective 8
- **Right:** base 3 + 5 padding → effective 8
- **Bottom:** base 3 → effective 3

Horizontal padding: `src/states/ReaderState.cpp:39`.

Resulting text viewport (portrait orientation):

- **X4** (480×800 portrait): 464×765 with status bar, 464×788 without
- **X3** (528×792 portrait): 512×757 with status bar, 512×780 without

- **Reader mode:** 464×788

Reader mode changes page layout by using the full viewport, but it allocates no additional buffers.

## Font Memory

### Builtin fonts (Flash)

Builtin fonts (`reader_2b`, `reader_bold_2b`, `reader_italic_2b`) are stored in Flash via `PROGMEM`. Bitmap data costs **0 bytes RAM**.

Each font has a `GlyphCache` for O(1) hot-glyph lookup (`lib/EpdFont/src/EpdFont.h:4-39`):

```
64 entries × 8 bytes (4B codepoint + 4B pointer) = 512 bytes per font
```

Three fonts (regular + bold + italic) = **1,536 bytes (~1.5KB)**.

### Streaming external fonts (.epdfont)

Streaming fonts (`lib/EpdFont/src/StreamingEpdFont.h`) load interval tables and glyph metadata into RAM but stream bitmap data from SD card with an LRU cache.

Typical RAM per font: **~25KB** (vs ~70KB if fully loaded). Breakdown:

- Interval + glyph tables: varies by font (~10-15KB)
- LRU bitmap cache: 64 entries, dynamically allocated per glyph
- Glyph lookup cache: 64 entries × 12 bytes = 768 bytes
- Hash table: 64 × 2 bytes = 128 bytes

Bold and italic variants are loaded **lazily** — 0 bytes until the first styled text is encountered. Each additional variant adds ~25KB.

### CJK external font (.bin)

The CJK fallback font (`lib/ExternalFont/src/ExternalFont.h`) is loaded lazily on the first CJK codepoint. It uses a fixed-size LRU cache:

```
CacheEntry = 4B codepoint + 200B bitmap + 4B lastUsed + 3B flags = ~211 bytes
64 entries × 211 bytes ≈ 13.5KB
+ hash table: 64 × 2B = 128 bytes
Total: ~13.6KB
```

## Rendering Support Buffers

Always allocated when `GfxRenderer` is constructed (`lib/GfxRenderer/src/GfxRenderer.h:61-76`):

- `bitmapOutputRow_` — 200 bytes (row output buffer, 800/4)
- `bitmapRowBytes_` — 2,400 bytes (24bpp row decode buffer, 800×3)
- Width cache keys — 2,048 bytes (256 × 8B hash keys)
- Width cache values — 512 bytes (256 × 2B pixel widths)
- **Total: ~5.2KB**

## Anti-Aliasing Pipeline

### Without AA

Simple BW render directly into the framebuffer. One render pass, no extra memory.

### With AA (grayscale text)

The AA pipeline reuses the **same 48KB framebuffer** for all passes — no backup buffer is allocated. From `src/states/ReaderState.cpp:776-809`:

1. Render BW page → `displayBuffer` (normal page flip)
2. `clearScreen(0x00)`, render LSB mask → `copyGrayscaleLsbBuffers` (SPI to SSD1677 BW RAM)
3. `clearScreen(0x00)`, render MSB mask → `copyGrayscaleMsbBuffers` (SPI to SSD1677 RED RAM)
4. `displayGrayBuffer()` → SSD1677 combines BW+RED RAM for 4-level grayscale
5. Re-render BW from scratch → `cleanupGrayscaleWithFrameBuffer` (restores RED RAM)
Line 791: *"Re-render BW instead of restoring from backup (saves 48KB peak allocation)"*

**Extra RAM: 0 bytes.** The cost is CPU time, not memory.

## Page Caching

Pages are laid out once and serialized to SD card so subsequent renders are reads, not re-layouts. Layout depends on viewport, font, hyphenation, and CSS, so the cache file is keyed by `fontId` and the on-disk render config — changing font invalidates the cache automatically (`lib/PageCache/src/PageCache.cpp` header layout, version 18).

### Chunked, partial cache

To stay within RAM, only a few pages are laid out at a time. `PageCache::DEFAULT_CACHE_CHUNK = 5` (`lib/PageCache/src/PageCache.h:24`):

1. `PageCache::create()` opens the cache file, calls `parser.parsePages(callback, maxPages=5, shouldAbort)`.
2. The parser emits `Page` objects via the callback. Each page is serialized to disk; its file offset is recorded in an in-memory LUT.
3. When 5 pages are emitted, the parser stops. `isPartial_ = parser.hasMoreContent()` records whether more text remains.
4. The LUT is appended after the page data and the header is rewritten with `pageCount`, `isPartial`, and the LUT offset.

Resulting layout:

```
[header (37 B)] [page 0] [page 1] ... [page N-1] [LUT: N × uint32 file offsets]
```

### Extension

When the user nears the end of the cached range (`PageCache::needsExtension()`), `PageCache::extend()` re-runs the parser from the start with `skipPages = pageCount_`: previously-cached pages are emitted-and-discarded by the callback until the parser reaches new content, then the next chunk is appended **after the old LUT**. The header is rewritten only after the new pages and new LUT are durable, so a power loss mid-extend leaves the previous cache intact.

(`ContentParser::canResume()` is a hook for hot-extend — currently true for EPUB only; non-EPUB parsers re-parse from the start.)

### Format-specific parsers

`ReaderState::createOrExtendCache()` (`src/states/ReaderState.cpp:1049`) picks a `ContentParser` (`lib/PageCache/src/ContentParser.h`) by `ContentType`:

- **EPUB** — `EpubChapterParser`
- **Markdown** — `MarkdownParser`
- **FB2** — `Fb2Parser` (gets language hint for hyphenation)
- **HTML** — `HtmlParser`
- **TXT** — `PlainTextParser`

EPUB caches one chapter per spine index (`epub_<hash>/sections/<idx>.bin`); other formats use a single `pages_<fontId>.bin`. See [docs/file-formats.md](file-formats.md) for the on-disk page record layout.

### Foreground vs. background

Two entry points:

**Foreground** — `renderCachedPage()` in `src/states/ReaderState.cpp:823`. If the current page isn't cached yet, draws an "Indexing..." overlay and calls `createOrExtendCache()` synchronously. Blocks UI.

**Background** — `startBackgroundCaching()` (`src/states/ReaderState.cpp:1252`) spawns a FreeRTOS task to extend the cache by another chunk while the user reads:

```
Stack: 12,288 bytes (12KB)
```

`stopBackgroundCaching()` requests cooperative cancellation via `AbortCallback`; the task self-deletes (never `vTaskDelete` — would corrupt mutexes; see `CLAUDE.md` threading rules).

### Ownership

Pages live on SD card, not in RAM. The ownership model avoids mutexes on `pageCache_` / `parser_`:

- Background task owns them while it's running.
- Main thread takes over only after `stopBackgroundCaching()` has confirmed the task self-deleted.

No simultaneous access → no mutex overhead on the hot path.

### When the total page count becomes exact

`pageCache_->pageCount()` reflects only the pages currently cached. Until `parser.hasMoreContent()` returns false, `isPartial_` stays true and the page total is an estimate.

## Memory Summary

### Builtin fonts (~67KB total)

Both AA states use the same RAM:

- Framebuffer: 48KB
- Fonts (3 glyph caches): ~1.5KB
- Render buffers: 5.2KB
- Cache task stack: 12KB
- AA overhead: 0 (re-renders instead of backup buffer)

### External fonts (~90KB total)

Both AA states use the same RAM:

- Framebuffer: 48KB
- Font (1 streaming regular): ~25KB
- Render buffers: 5.2KB
- Cache task stack: 12KB
- AA overhead: 0 (re-renders instead of backup buffer)

### Additive costs

- CJK fallback font: +~14KB (lazy, loaded on first CJK codepoint)
- Bold variant (external): +~25KB (lazy, loaded on first bold text)
- Italic variant (external): +~25KB (lazy, loaded on first italic text)

**Key insight:** AA does not affect peak RAM. The only variable that significantly changes memory usage is the font type (builtin vs external) and how many external font variants are loaded.

## Key Source Files

- `lib/EInkDisplay/include/EInkDisplay.h` — framebuffer, display constants
- `lib/GfxRenderer/src/GfxRenderer.h` — render buffers, width cache, grayscale API
- `lib/EpdFont/src/EpdFont.h` — builtin glyph cache
- `lib/EpdFont/src/StreamingEpdFont.h` — streaming font, LRU bitmap cache
- `lib/ExternalFont/src/ExternalFont.h` — CJK font, fixed-size LRU cache
- `src/states/ReaderState.cpp` — viewport, AA pipeline, background task
- `src/FontManager.cpp` — lazy font loading
- `platformio.ini` — `EINK_DISPLAY_SINGLE_BUFFER_MODE` flag
