# Architecture

This document describes the internal architecture and subsystems of Papyrix.

## Overview

Papyrix is organized around a **state machine** architecture with **singleton managers** and **content providers** for multi-format ebook support. The system is optimized for the ESP32-C3's ~380KB RAM constraint.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Application                                         │
├─────────────────────────────────────────────────────────────────────────────┤
│  StateMachine (10 States)  │  Managers (Font, Theme, Input)                 │
├─────────────────────────────────────────────────────────────────────────────┤
│  ContentHandle                                                              │
│  (EPUB, XTC, TXT, Markdown, FB2, HTML)               │  PageCache           │
├─────────────────────────────────────────────────────────────────────────────┤
│  GfxRenderer  │  EpdFont  │  ThaiShaper  │  ArabicShaper  │  ScriptDetector │
├─────────────────────────────────────────────────────────────────────────────┤
│  EInkDisplay  │  Storage  │  Input  │  Network                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                    ESP32-C3 Hardware                                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## State Machine

Papyrix uses a finite state machine (FSM) with pre-allocated state instances. This avoids heap allocation during state transitions, preventing memory fragmentation.

### States

- **Startup** — Initial boot, system initialization
- **Home** — Main hub with book card and navigation
- **FileList** — File browser for book selection
- **Reader** — Unified reader for all formats
- **Settings** — User preferences and device settings
- **Network** — WiFi network selection and connection
- **CalibreSync** — Calibre wireless device sync
- **AppLauncher** — Mini-apps, WiFi transfer, and Calibre sync
- **Error** — Error display and recovery
- **Sleep** — Deep sleep with custom screens

### State Lifecycle

```cpp
class State {
  virtual void enter(const StateTransition& transition);
  virtual StateTransition update();
  virtual void exit();
  virtual void render(GfxRenderer& gfx);
  virtual StateId id() const;
};
```

States use `StateTransition` to navigate between screens:
- `StateTransition::to(StateId)` - Navigate to another state
- `StateTransition::stay(StateId)` - Remain in current state

### Dual-Boot System

To maximize available RAM in reader mode, Papyrix implements a dual-boot system:

- **UI Mode**: Full feature set with all 10 states, theme switching, multiple font sizes
- **Reader Mode**: Minimal reader with only Reader/Sleep/Error states, single font size

The boot mode is stored in RTC memory and persists across ESP restarts. When launching a book from UI mode, the device restarts into Reader mode for maximum memory efficiency.

---

## Content System

### ContentHandle

`ContentHandle` is a tagged union that manages one content provider at a time, supporting:

- **EPUB** — `EpubProvider` — `.epub`
- **FB2** — `Fb2Provider` — `.fb2`
- **XTC** — `XtcProvider` — `.xtc`, `.xtch`
- **TXT** — `TxtProvider` — `.txt`, `.text`
- **Markdown** — `MarkdownProvider` — `.md`, `.markdown`

The unified interface provides:
- `open(path, cacheDir)` - Auto-detect format and open
- `pageCount()`, `spineCount()` - Navigation info
- `getTocEntry()` - Table of contents access
- `generateThumbnail()` - Cover image generation

### PageCache

Unified page caching system for all content types:

- **Partial caching**: 5 pages laid out at a time (`PageCache::DEFAULT_CACHE_CHUNK`) so peak RAM stays bounded.
- **Extend-on-demand**: When the reader approaches the cached tail, `PageCache::extend()` appends another chunk; the header is rewritten only after new data is durable, so power loss mid-extend leaves the previous cache intact.
- **Format-specific parsers**: each `ContentType` has a `ContentParser` (Epub / Fb2 / Html / Markdown / PlainText); the cache logic itself is shared.
- **Cache key**: `fontId` + render config; changing font invalidates automatically.
- **Background caching**: FreeRTOS task pre-renders ahead while the user reads, with an ownership model that needs no mutexes on `pageCache_` / `parser_`.
- **Serialization**: Pages written to SD card; in-memory LUT mapped from disk for O(1) page seek.

See [Rendering Pipeline § Page Caching](rendering-pipeline.md#page-caching) for the full flow and [File Formats](file-formats.md) for the on-disk page record layout.

### Progress Manager

Saves and restores reading position per book:
- Spine index (EPUB chapter)
- Section page (page within chapter)
- Flat page (XTC absolute page)

Cache location: `/.papyrix/<format>_<hash>/progress.bin`

---

## Memory Management

The ESP32-C3 has ~380KB usable RAM with ~100-150KB available after system overhead. Papyrix employs several strategies:

### Allocation Strategies

- **Pre-allocated states**: All 10 states allocated at startup, not during transitions
- **Fixed-size buffers**: Path (256), Text (512), Decompress (8192) in global Core struct
- **Tagged unions**: ContentHandle uses one provider at a time
- **Chunked buffers**: GfxRenderer splits display buffer into 8KB chunks for non-contiguous allocation

### WiFi Memory

The ESP32 WiFi stack allocates ~100KB and fragments heap memory. After using WiFi features, the device automatically restarts to reclaim memory before entering Reader mode.

### Caching

- **Compressed thumbnails**: 2-4KB vs 48KB uncompressed
- **Glyph lookup cache**: 64-entry direct-mapped cache per font (codepoint → glyph)
- **Glyph bitmap cache**: 128-entry LRU cache per streaming font (glyph → bitmap)
- **Word width cache**: 512-entry FNV-1a hash cache in GfxRenderer
- **SD card caching**: All parsed content cached to SD card

---

## Font System

### Pipeline

```
Storage → EpdFontLoader → FontManager → GfxRenderer → Display
```

1. **Storage**: Fonts loaded from flash (builtin) or SD card (custom)
2. **EpdFontLoader**: Parses `.epdfont` binary format, provides glyph lookup
3. **FontManager**: Manages font lifecycle, handles loading/unloading
4. **GfxRenderer**: Renders text using font glyphs
5. **Display**: Final output to e-paper

### Memory

- **Builtin fonts**: Flash (DROM), ~20 bytes RAM per wrapper
- **Custom fonts (streaming)**: ~25KB RAM per font (metadata + LRU cache)

### Streaming Font System

Custom fonts use `StreamingEpdFont` for memory efficiency:

- **Metadata in RAM**: Glyph table (~10-15KB) and unicode intervals (~2KB)
- **Bitmaps on SD**: Streamed on-demand, not stored in RAM
- **LRU cache**: 128-entry cache for recently-used glyph bitmaps
- **Hash table**: O(1) cache lookup with linear probing

Memory comparison for a typical 50KB font:
- **EpdFont (full load)**: ~70KB (intervals + glyphs + bitmap)
- **StreamingEpdFont**: ~25KB (intervals + glyphs + cache)

### Fallback Behavior

The font system guarantees users can always read:

1. **Font load failure** → Returns builtin font ID (FontManager.cpp)
2. **Streaming bitmap failure** → Skips character gracefully (GfxRenderer.cpp)
3. **Glyph not found** → Falls back to '?' character

Defensive checks in StreamingEpdFont:
- Bounds check on glyph index (corrupted font protection)
- Validates file handle before SD reads
- Rejects glyphs >4KB (corrupted data protection)
- Returns nullptr on partial SD read (SD card errors)

### `.epdfont` Format

Binary format with sections:

```
Header → Metrics → Unicode Intervals → Glyphs → Bitmap
```

- **Header**: Magic, version, font metadata
- **Metrics**: Line height, ascender, descender
- **Unicode Intervals**: Ranges of supported codepoints
- **Glyphs**: Per-character metrics and bitmap offsets
- **Bitmap**: 1-bit or 2-bit packed glyph data

### Key Files

- `lib/EpdFont/EpdFontLoader.cpp` — Format parsing, full and streaming load modes
- `lib/EpdFont/StreamingEpdFont.cpp` — Memory-efficient streaming font with LRU cache
- `src/FontManager.h/cpp` — Font lifecycle management, fallback handling
- `lib/GfxRenderer/` — Text rendering with streaming font integration
- `scripts/convert-fonts.mjs` — TTF/OTF to `.epdfont` conversion

### CJK Support

CJK fonts use binary search for glyph lookup: O(log n) complexity. Text can break at any character boundary (no word-based line breaking).

## CSS Parser

### Pipeline

```
EPUB Load → ContentOpfParser → CssParser → ChapterHtmlSlimParser → Page
```

1. **ContentOpfParser**: Discovers CSS files in EPUB manifest (media-type contains "css")
2. **CssParser**: Parses CSS files, builds style map keyed by selector
3. **ChapterHtmlSlimParser**: Queries CSS for each element, applies styles during page layout

### Supported Properties

- **text-align** (left, right, center, justify) — Block alignment
- **font-style** (normal, italic) — Italic text
- **font-weight** (normal, bold, 700+) — Bold text
- **text-indent** (px, em) — First-line indent
- **margin-top/bottom** (em, %) — Extra line spacing
- **direction** (ltr, rtl) — Text direction (RTL for Arabic)

### Supported Selectors

- **Tag selectors**: `p`, `div`, `span`
- **Class selectors**: `.classname`
- **Tag.class selectors**: `p.classname`
- **Comma-separated**: `h1, h2, h3`
- **Inline styles**: `style="text-align: center"`

### Key Files

- `lib/Epub/Epub/css/CssStyle.h` — Style enums and struct
- `lib/Epub/Epub/css/CssParser.h/cpp` — CSS file parsing
- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp` — Style application during HTML parsing

## Text Layout

### Line Breaking Algorithm

Papyrix uses the **Knuth-Plass algorithm** for optimal line breaking, the same algorithm used by TeX. This produces higher-quality justified text than greedy algorithms.

**Hyphenation**: The Liang algorithm (also from TeX) finds valid hyphenation points within words. Language is auto-detected from EPUB metadata (`<dc:language>`) and falls back to English. Supported languages: German, English, Spanish, French, Italian, Russian, Ukrainian. Binary trie patterns are sourced from [typst/hypher](https://github.com/typst/hypher).

```
Words → calculateWordWidths() → computeLineBreaks() → extractLine() → TextBlock
```

### How It Works

1. **Forward Dynamic Programming**: Evaluates all possible line break points
2. **Badness**: Measures line looseness using cubic ratio: `((target - actual) / target)³ × 100`
3. **Demerits**: Cost function `(1 + badness)²` penalizes loose lines
4. **Line Penalty**: Constant `+50` per line favors fewer total lines
5. **Last Line**: Zero demerits (allowed to be loose, as in book typography)

### Cost Function

```
badness = ((pageWidth - lineWidth) / pageWidth)³ × 100
demerits = (1 + badness)² + LINE_PENALTY
```

Lines exceeding page width get infinite penalty. Oversized words that can't fit are forced onto their own line with a fixed penalty.

### Key Files

- `lib/Epub/Epub/ParsedText.cpp` — Line breaking implementation
- `lib/Epub/Epub/ParsedText.h` — ParsedText class definition

### Reference

- Knuth, D. E., & Plass, M. F. (1981). *Breaking paragraphs into lines.* Software: Practice and Experience, 11(11), 1119-1184. [DOI:10.1002/spe.4380111102](https://doi.org/10.1002/spe.4380111102)

---

## Multi-Script Support

Papyrix supports multiple writing systems through script detection and specialized rendering.

### ScriptDetector

Classifies text by Unicode codepoint ranges:

- **LATIN** — Latin, Cyrillic, Greek — Word-based line breaking
- **CJK** — Chinese, Japanese, Korean (U+4E00–U+9FFF, etc.) — Character-based line breaking
- **THAI** — Thai script (U+0E00–U+0E7F) — Word segmentation
- **ARABIC** — Arabic script (U+0600–U+06FF, etc.) — Shaping and RTL layout
- **OTHER** — Symbols, digits, punctuation — Contextual line breaking

### Thai Text Rendering

Thai script requires special handling due to:
- **Vowel marks** above/below consonants
- **Tone marks** stacking above vowels
- **No spaces** between words

The ThaiShaper library provides:
- **ThaiCluster**: Groups consonants with marks into grapheme clusters
- **ThaiWordBreak**: Dictionary-based word segmentation for line breaking
- **Mark positioning**: Proper vertical ordering of diacritics

### Arabic Text Rendering

Arabic script is supported natively in the built-in fonts and requires special handling in reader mode for book text:
- **Contextual shaping**: Letters change form based on position (initial, medial, final, isolated)
- **Lam-Alef ligatures**: Automatic ligature formation for Lam + Alef combinations
- **RTL layout**: Words are rendered right-to-left with right-aligned lines
- **CSS direction**: `direction: rtl` in EPUB stylesheets triggers RTL paragraph layout

The ArabicShaper library converts logical-order UTF-8 text to visual-order shaped codepoints for left-to-right rendering by the font system.

### CJK Rendering

CJK text uses ExternalFont for large character set support:
- **LRU cache**: 256-entry cache (~52KB) for glyph bitmaps
- **Binary search**: O(log n) glyph lookup in large fonts
- **Character-level breaking**: No word boundaries needed

---

## Rendering Pipeline

### Flow

```
Content → ContentParser → Page → GfxRenderer → EInkDisplay
```

1. **ContentParser**: Converts format-specific content to `Page` objects
2. **Page**: Contains `PageLine` (text) and `PageImage` elements
3. **GfxRenderer**: Renders pages using fonts and themes
4. **EInkDisplay**: Final output with refresh mode control

### GfxRenderer Features

- **Render modes**: BW (1-bit), Grayscale LSB, Grayscale MSB
- **Orientation**: Portrait, Landscape CW/CCW, Inverted
- **Word caching**: 512-entry hash cache for repeated word widths
- **Row buffers**: Pre-allocated to avoid per-line allocation

### Refresh Modes

- **Full** — Complete redraw, clears ghosting (no ghosting)
- **Partial** — Fast page turns (some ghosting)
- **Fast** — Animation, menus (more ghosting)

The "Pages Per Refresh" setting controls how often full refresh occurs (1/5/10/15/30 pages).

---

## Image Rendering

EPUB images (JPEG/PNG/BMP) are converted to BMP and cached to SD card. Data URIs are stripped before parsing to prevent OOM. See [images.md](images.md) for details.

---

## UI System

Papyrix uses a view-based UI architecture with reusable elements and state-driven rendering.

### Directory Structure

```
src/ui/
├── Elements.h/cpp          # Reusable UI components
├── Views.h                 # Unified header for all views
└── views/                  # Screen-specific views
    ├── HomeView.h/cpp      # Home screen with book card
    ├── ReaderViews.h/cpp   # Reader UI (TOC, overlays)
    ├── SettingsViews.h/cpp # Settings screens
    ├── NetworkViews.h/cpp  # WiFi configuration
    ├── AppLauncherViews.h/cpp # App launcher menu
    ├── CalibreViews.h/cpp  # Calibre sync UI
    ├── UtilityViews.h/cpp  # Common elements
    └── BootSleepViews.h/cpp# Boot splash, sleep screen
```

### UI Elements

The `ui::` namespace provides reusable rendering components:

- **`ButtonBar`** — 4-button hint bar at screen bottom
- **`title()`** — Centered bold heading
- **`menuItem()`** — Selectable menu entry
- **`toggle()`** — On/Off setting row
- **`enumValue()`** — Setting with value display
- **`keyboard()`** — On-screen keyboard (10x10 grid)
- **`battery()`** — Battery icon with percentage
- **`bookCard()`** — Cover + title + author
- **`fileEntry()`** — File name with directory indicator
- **`chapterItem()`** — TOC entry with depth indentation
- **`wifiEntry()`** — Network + signal + lock icon
- **`dialog()`** — Yes/No confirmation
- **`readerStatusBar()`** — Battery, title, page numbers (chapter page count available only after caching)

### ButtonBar Pattern

Views use `ButtonBar` to define which buttons are active and their labels:

```cpp
ui::ButtonBar buttons("Back", "Select", "", "");  // 2 active buttons
ui::buttonBar(renderer, theme, buttons);
```

### View Pattern

Views are stateless rendering functions. States own the data and call views:

```cpp
// State owns data
class HomeState : public State {
    BookMetadata currentBook_;
    int selectedIndex_;

    void render(GfxRenderer& gfx) override {
        HomeView::render(gfx, theme, currentBook_, selectedIndex_);
    }
};
```

---

## Desktop Testing (reader-test)

`tools/reader-test/` is a desktop tool that runs the full content parsing pipeline (EPUB/FB2/HTML/TXT/Markdown) without hardware. It uses the same built-in fonts and viewport dimensions as the device to produce identical page boundaries.

### Device Emulation

- **Real font metrics**: Uses `reader_2b`, `reader_bold_2b`, `reader_italic_2b` built-in fonts with per-glyph `advanceX` lookup (not fixed-width approximation)
- **Device viewport**: 464x788 pixels (480 - 2*(3+5) x 800 - 9 - 3)
- **Batched caching**: `--batch 5` emulates the device's batched page cache generation with suspend/resume cycles
- **Status bar**: removed from reader mode; the full viewport is always used
- **Font ID**: `READER_FONT_ID = 1818981670`, same as device

### Architecture

```
tools/reader-test/
├── main.cpp              # CLI entry, font registration, content dispatch
├── CMakeLists.txt        # Build config (links real EpdFont, Utf8, parsers)
└── mocks/
    ├── GfxRenderer.h     # Real text metrics, no-op drawing
    ├── EInkDisplay.h     # Stub display (buffer only)
    ├── SDCardManager.h   # Maps SD calls to filesystem
    └── platform_stubs.cpp # Arduino/FreeRTOS stubs
```

The mock `GfxRenderer` provides real text measurement (`getTextWidth`, `getSpaceWidth`, `getLineHeight`, `getFontAscenderSize`, `breakWordWithHyphenation`) using the font map, while all drawing methods are no-ops.

### Usage

```bash
# Parse book with device-matching batch mode
reader-test --dump --batch 5 book.epub /tmp/cache

# Parse with reader viewport
reader-test --dump book.epub /tmp/cache

# Dump text from device cache (copied from SD card)
reader-test --cache-dump /path/to/.papyrix/epub_<hash>/

# Compare to find text differences (missing/duplicated text)
diff <(reader-test --dump --batch 5 book.epub /tmp/cache 2>/dev/null) \
     <(reader-test --cache-dump /path/to/device-cache/ 2>/dev/null)
```

### Verifying Parser Fixes

To verify fixes to the parsing/caching pipeline:

1. Build reader-test **without** the fix, run with `--batch 5`, save output
2. Apply the fix, rebuild, run again
3. Diff the outputs — recovered text confirms the fix works

The `--batch 5` flag is critical for reproducing suspend/resume bugs that only trigger at batch boundaries during page cache generation.

---

## Key Files

### Core (`/src/core/`)

- **`Core.h`** — Global state, drivers, buffers
- **`StateMachine.h`** — FSM implementation
- **`Types.h`** — Enums and constants
- **`BootMode.h`** — Dual-boot system
- **`PapyrixSettings.h`** — User preferences

### States (`/src/states/`)

- **`State.h`** — Base state interface
- **`ReaderState.h`** — Unified reader (largest state)
- **`HomeState.h`** — Main hub with async cover loading
- **`SettingsState.h`** — Preferences UI

### Content (`/src/content/`)

- **`ContentHandle.h`** — Tagged union for providers
- **`EpubProvider.h`** — EPUB format support
- **`Fb2Provider.h`** — FB2 (FictionBook 2.0) format support
- **`XtcProvider.h`** — XTC/XTCH format support
- **`TxtProvider.h`** — Plain text support
- **`MarkdownProvider.h`** — Markdown format support
- **`ProgressManager.h`** — Reading position persistence
- **`ReaderNavigation.h`** — Page/chapter traversal

### UI (`/src/ui/`)

- **`Elements.h`** — Reusable UI components (ButtonBar, keyboard, etc.)
- **`Views.h`** — Unified header for all view types
- **`views/HomeView.h`** — Home screen rendering
- **`views/ReaderViews.h`** — Reader UI (TOC, overlays)
- **`views/SettingsViews.h`** — Settings screen rendering

### Libraries (`/lib/`)

- **`Epub/`** — EPUB parsing, CSS, TOC
- **`Fb2/`** — FB2 (FictionBook 2.0) parsing, metadata extraction, TOC
- **`Xtc/`** — XTC/XTCH native format
- **`Txt/`** — Plain text file handling
- **`Markdown/`** — Markdown format support
- **`PageCache/`** — Unified page caching
- **`GfxRenderer/`** — Graphics rendering
- **`EpdFont/`** — Font loading (full and streaming modes) and glyph cache
- **`ExternalFont/`** — CJK font support
- **`ScriptDetector/`** — Script classification
- **`ArabicShaper/`** — Arabic text shaping (contextual forms, ligatures)
- **`ThaiShaper/`** — Thai text shaping
- **`Hyphenation/`** — Liang-pattern hyphenation with language-specific tries (de, en, es, fr, it, ru, uk)
- **`Utf8/`** — UTF-8 string utilities
- **`ZipFile/`** — EPUB ZIP extraction
- **`Group5/`** — 1-bit image compression
- **`Calibre/`** — Calibre wireless sync protocol
- **`ImageConverter/`** — JPEG/PNG to BMP conversion
- **`Serialization/`** — Binary serialization utilities
