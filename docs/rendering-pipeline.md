# Rendering Pipeline Memory Usage (Reader Mode)

This document describes RAM use in **Reader mode** for all combinations of anti-aliasing (AA), font type, and status bar settings. Reader mode is the mode with the smallest memory use. It is made for reading. WiFi is off. This frees approximately 100KB compared to UI mode.

## Display Framebuffer

The device uses single-buffer mode (`-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1` in `platformio.ini`).

One framebuffer is allocated as static memory. The size is for the larger panel:

- **X4:** 800 × 480 — 100 bytes/row × 480 = 48,000 bytes
- **X3:** 792 × 528 — 99 bytes/row × 528 = 52,272 bytes

The static buffer is allocated at `MAX_BUFFER_SIZE = 52,272` bytes to support the two panels (`lib/EInkDisplay/include/EInkDisplay.h`). This buffer is always there. You cannot free it.

## Viewport Dimensions

Base margins from `GfxRenderer` (`lib/GfxRenderer/src/GfxRenderer.h`):

- **Top:** base 9 → effective 9
- **Left:** base 3 + 5 padding → effective 8
- **Right:** base 3 + 5 padding → effective 8
- **Bottom:** base 3 (+23 if status bar) → effective 3 or 26

Horizontal padding and status bar margin: `src/states/ReaderState.cpp:39-40`.

Resulting text viewport (portrait orientation):

- **X4** (480×800 portrait): 464×765 with status bar, 464×788 with no status bar
- **X3** (528×792 portrait): 512×757 with status bar, 512×780 with no status bar

Viewport dimensions are calculated from `GfxRenderer::getScreenWidth()` / `getScreenHeight()`. These read the panel size from `EInkDisplay`. The same code serves the two panels.

The status bar changes page layout (fewer lines for each page) but allocates **no more buffers**. The status bar is drawn into the same framebuffer.

## Font Memory

### Builtin fonts (Flash)

Builtin fonts (`reader_2b`, `reader_bold_2b`, `reader_italic_2b`) are stored in Flash through `PROGMEM`. Bitmap data costs **0 bytes RAM**.

Each font has a `GlyphCache` for O(1) hot-glyph lookup (`lib/EpdFont/src/EpdFont.h:4-39`):

```
64 entries × 8 bytes (4B codepoint + 4B pointer) = 512 bytes per font
```

Three fonts (regular + bold + italic) = **1,536 bytes (approximately 1.5KB)**.

### Streaming external fonts (.epdfont)

Streaming fonts (`lib/EpdFont/src/StreamingEpdFont.h`) load interval tables and glyph metadata into RAM. They stream bitmap data from the SD card with an LRU cache.

Usual RAM for each font: **approximately 25KB** (compared to approximately 70KB if fully loaded). Breakdown:

- Interval + glyph tables: different for each font (approximately 10-15KB)
- LRU bitmap cache: 64 entries, allocated for each glyph when necessary
- Glyph lookup cache: 64 entries × 12 bytes = 768 bytes
- Hash table: 64 × 2 bytes = 128 bytes

Bold and italic variants are loaded **when they are necessary** — 0 bytes until the first styled text occurs. Each added variant adds approximately 25KB.

### CJK external font (.bin)

The CJK fallback font (`lib/ExternalFont/src/ExternalFont.h`) is loaded when the first CJK codepoint occurs. It uses an LRU cache with a fixed size:

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
- **Total: approximately 5.2KB**

## Anti-Aliasing Pipeline

### Without AA

Simple BW render directly into the framebuffer. One render pass. No more memory.

### With AA (grayscale text)

The AA pipeline uses the **same 48KB framebuffer** for all passes. No backup buffer is allocated. From `src/states/ReaderState.cpp:776-809`:

1. Render BW page + status bar → `displayBuffer` (usual page flip)
2. `clearScreen(0x00)`, render LSB mask → `copyGrayscaleLsbBuffers` (SPI to SSD1677 BW RAM)
3. `clearScreen(0x00)`, render MSB mask → `copyGrayscaleMsbBuffers` (SPI to SSD1677 RED RAM)
4. `displayGrayBuffer()` → SSD1677 combines BW+RED RAM for 4-level grayscale
5. Render BW again from the start → `cleanupGrayscaleWithFrameBuffer` (puts RED RAM back)
6. If status bar: blank + draw again through `displayWindow` (status bar is 1-bit only)

Line 791: *"Re-render BW instead of restoring from backup (saves 48KB peak allocation)"*

**Extra RAM: 0 bytes.** The cost is CPU time — 3 more full-page renders (LSB, MSB, BW restore) plus status bar refresh.

## Page Caching

Pages are laid out one time and written to the SD card. Later renders are reads, not new layouts. Layout depends on viewport, font, hyphenation, and CSS. The cache file key is `fontId` and the on-disk render config. A font change makes the cache not valid (`lib/PageCache/src/PageCache.cpp` header layout, version 18).

### Chunked, partial cache

To stay in the RAM limit, only some pages are laid out at a time. `PageCache::DEFAULT_CACHE_CHUNK = 5` (`lib/PageCache/src/PageCache.h:24`):

1. `PageCache::create()` opens the cache file, calls `parser.parsePages(callback, maxPages=5, shouldAbort)`.
2. The parser sends `Page` objects through the callback. Each page is written to disk. Its file offset is recorded in an in-memory LUT.
3. When 5 pages are sent, the parser stops. `isPartial_ = parser.hasMoreContent()` records if more text remains.
4. The LUT is added after the page data. The header is written again with `pageCount`, `isPartial`, and the LUT offset.

Resulting layout:

```
[header (37 B)] [page 0] [page 1] ... [page N-1] [LUT: N × uint32 file offsets]
```

### Extension

When the user is near the end of the cached range (`PageCache::needsExtension()`), `PageCache::extend()` runs the parser again from the start with `skipPages = pageCount_`. Pages that are already cached are sent and discarded by the callback until the parser gets to new content. Then the next chunk is added **after the old LUT**. The header is written again only after the new pages and the new LUT are durable. If power is lost during extend, the previous cache stays.

(`ContentParser::canResume()` is a hook for hot-extend. It is true for EPUB only at this time. Non-EPUB parsers parse again from the start.)

### Format-specific parsers

`ReaderState::createOrExtendCache()` (`src/states/ReaderState.cpp:1049`) selects a `ContentParser` (`lib/PageCache/src/ContentParser.h`) by `ContentType`:

- **EPUB** — `EpubChapterParser`
- **Markdown** — `MarkdownParser`
- **FB2** — `Fb2Parser` (gets language hint for hyphenation)
- **HTML** — `HtmlParser`
- **TXT** — `PlainTextParser`

EPUB caches one chapter for each spine index (`epub_<hash>/sections/<idx>.bin`). Other formats use one `pages_<fontId>.bin`. See [docs/file-formats.md](file-formats.md) for the on-disk page record layout.

### Foreground vs. background

Three entry points:

**Full book pre-processing** — When the "Full Book Process" setting is on, `startFullBookIndexing()` runs before usual reading starts. It processes each spine/section in the main loop (one spine for each frame tick). It shows a progress bar. Sections that are already cached are skipped. The user can cancel with **Back**. After this completes, all page counts are exact. Background cache continues as usual. Skipped for XTC/XTCH files.

**Foreground** — `renderCachedPage()` in `src/states/ReaderState.cpp`. If the current page is not cached yet, draws an "Indexing..." overlay and calls `createOrExtendCache()` synchronously. Blocks UI.

**Background** — `startBackgroundCaching()` (`src/states/ReaderState.cpp`) starts a FreeRTOS task to extend the cache by one more chunk while the user reads:

```
Stack: 12,288 bytes (12KB)
```

`stopBackgroundCaching()` requests cooperative cancellation through `AbortCallback`. The task deletes itself (never `vTaskDelete` — that would damage mutexes; see `CLAUDE.md` threading rules).

### Ownership

Pages stay on the SD card, not in RAM. The ownership model prevents mutexes on `pageCache_` / `parser_`:

- The background task owns them while it runs.
- The main thread takes them only after `stopBackgroundCaching()` confirms that the task deleted itself.

No access at the same time → no mutex overhead on the hot path.

### When the total page count becomes exact

`pageCache_->pageCount()` shows only the pages that are cached at this time. Until `parser.hasMoreContent()` returns false, `isPartial_` stays true and the page total is an estimate. The reader status bar shows this with a `~` suffix on the total. See [User Guide § Reading Mode](user_guide.md#status-bar) for the user-visible meaning.

## Memory Summary

### Builtin fonts (approximately 67KB total)

All four combinations (AA on/off × status bar on/off) use the same RAM:

- Framebuffer: 48KB
- Fonts (3 glyph caches): approximately 1.5KB
- Render buffers: 5.2KB
- Cache task stack: 12KB
- AA overhead: 0 (renders again, no backup buffer)

### External fonts (approximately 90KB total)

All four combinations (AA on/off × status bar on/off) use the same RAM:

- Framebuffer: 48KB
- Font (1 streaming regular): approximately 25KB
- Render buffers: 5.2KB
- Cache task stack: 12KB
- AA overhead: 0 (renders again, no backup buffer)

### Additive costs

- CJK fallback font: + approximately 14KB (loaded on the first CJK codepoint)
- Bold variant (external): + approximately 25KB (loaded on the first bold text)
- Italic variant (external): + approximately 25KB (loaded on the first italic text)

**Primary fact:** AA and status bar settings do not change peak RAM. The only variable that changes memory use by a large amount is the font type (builtin compared to external) and how many external font variants are loaded.

## Key Source Files

- `lib/EInkDisplay/include/EInkDisplay.h` — framebuffer, display constants
- `lib/GfxRenderer/src/GfxRenderer.h` — render buffers, width cache, grayscale API
- `lib/EpdFont/src/EpdFont.h` — builtin glyph cache
- `lib/EpdFont/src/StreamingEpdFont.h` — streaming font, LRU bitmap cache
- `lib/ExternalFont/src/ExternalFont.h` — CJK font, fixed-size LRU cache
- `src/states/ReaderState.cpp` — viewport, AA pipeline, background task
- `src/FontManager.cpp` — font load when necessary
- `platformio.ini` — `EINK_DISPLAY_SINGLE_BUFFER_MODE` flag
