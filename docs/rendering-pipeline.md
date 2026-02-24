# Rendering Pipeline Memory Usage (Reader Mode)

This document describes RAM usage in **Reader mode** across all combinations of anti-aliasing (AA), font type, and status bar settings. Reader mode is the minimal-footprint mode optimized for reading — WiFi is off, freeing ~100KB compared to UI mode.

## Display Framebuffer

The device uses single-buffer mode (`-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1` in `platformio.ini`).

One statically-allocated framebuffer:

```
800 × 480 / 8 = 48,000 bytes (48KB)
```

Defined as `frameBuffer0[BUFFER_SIZE]` in `lib/EInkDisplay/include/EInkDisplay.h:75`. This is always present — it cannot be freed.

## Viewport Dimensions

Base margins from `GfxRenderer` (`lib/GfxRenderer/src/GfxRenderer.h:110-113`):

- **Top:** base 9 → effective 9
- **Left:** base 3 + 5 padding → effective 8
- **Right:** base 3 + 5 padding → effective 8
- **Bottom:** base 3 (+23 if status bar) → effective 3 or 26

Horizontal padding and status bar margin: `src/states/ReaderState.cpp:39-40`.

Resulting text viewport:

- **Status bar ON:** 464×765
- **Status bar OFF:** 464×788

The status bar changes page layout (fewer lines per page) but allocates **no additional buffers**. The status bar is rendered into the same framebuffer.

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

1. Render BW page + status bar → `displayBuffer` (normal page flip)
2. `clearScreen(0x00)`, render LSB mask → `copyGrayscaleLsbBuffers` (SPI to SSD1677 BW RAM)
3. `clearScreen(0x00)`, render MSB mask → `copyGrayscaleMsbBuffers` (SPI to SSD1677 RED RAM)
4. `displayGrayBuffer()` → SSD1677 combines BW+RED RAM for 4-level grayscale
5. Re-render BW from scratch → `cleanupGrayscaleWithFrameBuffer` (restores RED RAM)
6. If status bar: blank + redraw via `displayWindow` (status bar is 1-bit only)

Line 791: *"Re-render BW instead of restoring from backup (saves 48KB peak allocation)"*

**Extra RAM: 0 bytes.** The cost is CPU time — 3 additional full-page renders (LSB, MSB, BW restore) plus status bar refresh.

## Background Caching Task

The page pre-caching task runs on a FreeRTOS background thread (`src/states/ReaderState.cpp:35`):

```
Stack: 12,288 bytes (12KB)
```

Pages are cached to SD card, not held in RAM. The ownership model ensures the main thread and background task never access the parser or page cache simultaneously — no mutex overhead on shared data.

## Memory Summary

### Builtin fonts (~67KB total)

All four combinations (AA on/off × status bar on/off) use the same RAM:

- Framebuffer: 48KB
- Fonts (3 glyph caches): ~1.5KB
- Render buffers: 5.2KB
- Cache task stack: 12KB
- AA overhead: 0 (re-renders instead of backup buffer)

### External fonts (~90KB total)

All four combinations (AA on/off × status bar on/off) use the same RAM:

- Framebuffer: 48KB
- Font (1 streaming regular): ~25KB
- Render buffers: 5.2KB
- Cache task stack: 12KB
- AA overhead: 0 (re-renders instead of backup buffer)

### Additive costs

- CJK fallback font: +~14KB (lazy, loaded on first CJK codepoint)
- Bold variant (external): +~25KB (lazy, loaded on first bold text)
- Italic variant (external): +~25KB (lazy, loaded on first italic text)

**Key insight:** AA and status bar settings do not affect peak RAM. The only variable that significantly changes memory usage is the font type (builtin vs external) and how many external font variants are loaded.

## Key Source Files

- `lib/EInkDisplay/include/EInkDisplay.h` — framebuffer, display constants
- `lib/GfxRenderer/src/GfxRenderer.h` — render buffers, width cache, grayscale API
- `lib/EpdFont/src/EpdFont.h` — builtin glyph cache
- `lib/EpdFont/src/StreamingEpdFont.h` — streaming font, LRU bitmap cache
- `lib/ExternalFont/src/ExternalFont.h` — CJK font, fixed-size LRU cache
- `src/states/ReaderState.cpp` — viewport, AA pipeline, background task
- `src/FontManager.cpp` — lazy font loading
- `platformio.ini` — `EINK_DISPLAY_SINGLE_BUFFER_MODE` flag
