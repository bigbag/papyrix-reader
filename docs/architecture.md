# Architecture

This document describes the internal architecture and subsystems of Papyrix.

## Overview

Papyrix uses a **state machine** architecture with **singleton managers** and **content providers** for ebook support in more than one format. The system is made for the ESP32-C3 limit of approximately 380KB RAM.

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
│  EInkDisplay  │  Storage  │  Input  │  Network  │  Device                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                    ESP32-C3 Hardware                                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## State Machine

Papyrix uses a finite state machine (FSM) with state instances that are allocated before use. This prevents heap allocation during state transitions. This prevents memory fragmentation.

### States

- **Startup** — Initial start, system initialization
- **Home** — Primary hub with book card and navigation
- **FileList** — File browser for book selection
- **Reader** — One reader for all formats
- **Settings** — User preferences and device settings
- **Network** — WiFi connection and file transfer (automatic connection to saved networks, manual network selection, hotspot mode)
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

States use `StateTransition` to go between screens:
- `StateTransition::to(StateId)` - Go to a different state
- `StateTransition::stay(StateId)` - Stay in the current state

### Dual-Boot System

To get maximum available RAM in reader mode, Papyrix uses a dual-boot system:

- **UI Mode**: Full feature set with all 10 states, theme change, more than one font size
- **Reader Mode**: Minimum reader with only Reader/Sleep/Error states, one font size

The boot mode is stored in RTC memory. It stays across ESP restarts. When you open a book from UI mode, the device restarts into Reader mode to get maximum memory.

---

## Content System

### ContentHandle

`ContentHandle` is a tagged union that manages one content provider at a time. It supports:

- **EPUB** — `EpubProvider` — `.epub`
- **FB2** — `Fb2Provider` — `.fb2`
- **XTC** — `XtcProvider` — `.xtc`, `.xtch`
- **TXT** — `TxtProvider` — `.txt`, `.text`
- **Markdown** — `MarkdownProvider` — `.md`, `.markdown`

The shared interface gives:
- `open(path, cacheDir)` - Find the format and open
- `pageCount()`, `spineCount()` - Navigation data
- `getTocEntry()` - Table of contents access
- `generateThumbnail()` - Cover image generation

### PageCache

One page cache system for all content types:

- **Partial caching**: 5 pages laid out at a time (`PageCache::DEFAULT_CACHE_CHUNK`) so peak RAM stays in a limit.
- **Extend-on-demand**: When the reader is near the cached tail, `PageCache::extend()` adds one more chunk. The header is written again only after new data is durable. If power is lost during extend, the previous cache stays.
- **Format-specific parsers**: each `ContentType` has a `ContentParser` (Epub / Fb2 / Html / Markdown / PlainText). The cache logic is shared.
- **Cache key**: `fontId` + render config. A font change makes the cache not valid.
- **Background caching**: A FreeRTOS task prepares pages ahead while the user reads. An ownership model needs no mutexes on `pageCache_` / `parser_`.
- **Serialization**: Pages written to the SD card. In-memory LUT mapped from disk for O(1) page seek.

See [Rendering Pipeline § Page Caching](rendering-pipeline.md#page-caching) for the full flow. See [File Formats](file-formats.md) for the on-disk page record layout.

### Progress Manager

Saves and restores reading position for each book:
- Spine index (EPUB chapter)
- Section page (page in the chapter)
- Flat page (XTC absolute page)

Cache location: `/.papyrix/<format>_<hash>/progress.bin`

---

## Memory Management

The ESP32-C3 has approximately 380KB usable RAM. Approximately 100-150KB is available after system overhead. Papyrix uses these strategies:

### Allocation Strategies

- **Pre-allocated states**: All 10 states allocated at start, not during transitions
- **Fixed-size buffers**: Path (256), Text (512), Decompress (8192) in the global Core struct
- **Tagged unions**: ContentHandle uses one provider at a time
- **Chunked buffers**: GfxRenderer splits the display buffer into 8KB chunks for allocation that is not contiguous

### WiFi Memory

The ESP32 WiFi stack allocates approximately 100KB and fragments heap memory. After you use WiFi features, the device restarts to get memory back before Reader mode.

### Caching

- **Compressed thumbnails**: 2-4KB compared to 48KB uncompressed
- **Glyph lookup cache**: 64-entry direct-mapped cache for each font (codepoint → glyph)
- **Glyph bitmap cache**: 128-entry LRU cache for each streaming font (glyph → bitmap)
- **Word width cache**: 512-entry FNV-1a hash cache in GfxRenderer
- **SD card caching**: All parsed content cached to the SD card

---

## Font System

### Pipeline

```
Storage → EpdFontLoader → FontManager → GfxRenderer → Display
```

1. **Storage**: Fonts loaded from flash (builtin) or SD card (custom)
2. **EpdFontLoader**: Parses `.epdfont` binary format, gives glyph lookup
3. **FontManager**: Manages font lifecycle, handles load/unload
4. **GfxRenderer**: Shows text with font glyphs
5. **Display**: Final output to e-paper

### Memory

- **Builtin fonts**: Flash (DROM), approximately 20 bytes RAM for each wrapper
- **Custom fonts (streaming)**: approximately 25KB RAM for each font (metadata + LRU cache)

### Streaming Font System

Custom fonts use `StreamingEpdFont` to use less memory:

- **Metadata in RAM**: Glyph table (approximately 10-15KB) and unicode intervals (approximately 2KB)
- **Bitmaps on SD**: Streamed when necessary, not stored in RAM
- **LRU cache**: 128-entry cache for glyph bitmaps that were used recently
- **Hash table**: O(1) cache lookup with linear probing

Memory comparison for a usual 50KB font:
- **EpdFont (full load)**: approximately 70KB (intervals + glyphs + bitmap)
- **StreamingEpdFont**: approximately 25KB (intervals + glyphs + cache)

### Fallback Behavior

The font system makes sure that users can always read:

1. **Font load failure** → Returns builtin font ID (FontManager.cpp)
2. **Streaming bitmap failure** → Skips the character (GfxRenderer.cpp)
3. **Glyph not found** → Uses the '?' character

Defensive checks in StreamingEpdFont:
- Bounds check on glyph index (protection from a damaged font)
- Validates file handle before SD reads
- Rejects glyphs larger than 4KB (protection from damaged data)
- Returns nullptr on a partial SD read (SD card errors)

### `.epdfont` Format

Binary format with sections:

```
Header → Metrics → Unicode Intervals → Glyphs → Bitmap
```

- **Header**: Magic, version, font metadata
- **Metrics**: Line height, ascender, descender
- **Unicode Intervals**: Ranges of supported codepoints
- **Glyphs**: Metrics and bitmap offsets for each character
- **Bitmap**: 1-bit or 2-bit packed glyph data

### Key Files

- `lib/EpdFont/EpdFontLoader.cpp` — Format parse, full and streaming load modes
- `lib/EpdFont/StreamingEpdFont.cpp` — Streaming font that uses less memory, with LRU cache
- `src/FontManager.h/cpp` — Font lifecycle management, fallback handling
- `lib/GfxRenderer/` — Text rendering with streaming font integration
- `scripts/convert-fonts.mjs` — TTF/OTF to `.epdfont` conversion

### CJK Support

CJK fonts use binary search for glyph lookup: O(log n) complexity. Text can break at each character boundary (no word-based line break).

## CSS Parser

### Pipeline

```
EPUB Load → ContentOpfParser → CssParser → ChapterHtmlSlimParser → Page
```

1. **ContentOpfParser**: Finds CSS files in the EPUB manifest (media-type contains "css")
2. **CssParser**: Parses CSS files, builds a style map keyed by selector
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
- `lib/Epub/Epub/css/CssParser.h/cpp` — CSS file parse
- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp` — Style application during HTML parse

## Text Layout

### Line Breaking Algorithm

Papyrix uses the **Knuth-Plass algorithm** for the best line breaks. This is the same algorithm that TeX uses. This makes justified text of higher quality than greedy algorithms.

**Hyphenation**: The Liang algorithm (also from TeX) finds valid hyphenation points in words. Language is found from EPUB metadata (`<dc:language>`). If there is no language, English is used. Supported languages: German, English, Spanish, French, Italian, Russian, Ukrainian. Binary trie patterns come from [typst/hypher](https://github.com/typst/hypher).

```
Words → calculateWordWidths() → computeLineBreaks() → extractLine() → TextBlock
```

### How It Works

1. **Forward Dynamic Programming**: Examines all possible line break points
2. **Badness**: Measures line looseness with cubic ratio: `((target - actual) / target)³ × 100`
3. **Demerits**: Cost function `(1 + badness)²` gives a penalty to loose lines
4. **Line Penalty**: Constant `+50` for each line. This favors fewer total lines
5. **Last Line**: Zero demerits (can be loose, as in book typography)

### Cost Function

```
badness = ((pageWidth - lineWidth) / pageWidth)³ × 100
demerits = (1 + badness)² + LINE_PENALTY
```

Lines that are wider than the page width get an infinite penalty. Oversized words that cannot fit go on their own line with a fixed penalty.

### Key Files

- `lib/Epub/Epub/ParsedText.cpp` — Line break implementation
- `lib/Epub/Epub/ParsedText.h` — ParsedText class definition

### Reference

- Knuth, D. E., & Plass, M. F. (1981). *Breaking paragraphs into lines.* Software: Practice and Experience, 11(11), 1119-1184. [DOI:10.1002/spe.4380111102](https://doi.org/10.1002/spe.4380111102)

---

## Multi-Script Support

Papyrix supports more than one writing system through script detection and special rendering.

### ScriptDetector

Classifies text by Unicode codepoint ranges:

- **LATIN** — Latin, Cyrillic, Greek — Word-based line break
- **CJK** — Chinese, Japanese, Korean (U+4E00–U+9FFF, and more) — Character-based line break
- **THAI** — Thai script (U+0E00–U+0E7F) — Word segmentation
- **ARABIC** — Arabic script (U+0600–U+06FF, and more) — Shaping and RTL layout
- **OTHER** — Symbols, digits, punctuation — Contextual line break

### Thai Text Rendering

Thai script needs special handling because of:
- **Vowel marks** above/below consonants
- **Tone marks** stacking above vowels
- **No spaces** between words

The ThaiShaper library gives:
- **ThaiCluster**: Groups consonants with marks into grapheme clusters
- **ThaiWordBreak**: Dictionary-based word segmentation for line break
- **Mark positioning**: Correct vertical order of diacritics

### Arabic Text Rendering

Arabic script is supported in the built-in fonts. It needs special handling in reader mode for book text:
- **Contextual shaping**: Letters change form from position (initial, medial, final, isolated)
- **Lam-Alef ligatures**: Automatic ligature formation for Lam + Alef combinations
- **RTL layout**: Words are shown right-to-left with right-aligned lines
- **CSS direction**: `direction: rtl` in EPUB stylesheets starts RTL paragraph layout

The ArabicShaper library converts logical-order UTF-8 text to visual-order shaped codepoints for left-to-right rendering by the font system.

### CJK Rendering

CJK text uses ExternalFont for support of a large character set:
- **LRU cache**: 256-entry cache (approximately 52KB) for glyph bitmaps
- **Binary search**: O(log n) glyph lookup in large fonts
- **Character-level breaking**: No word boundaries are necessary

---

## Rendering Pipeline

### Flow

```
Content → ContentParser → Page → GfxRenderer → EInkDisplay
```

1. **ContentParser**: Converts format-specific content to `Page` objects
2. **Page**: Contains `PageLine` (text) and `PageImage` elements
3. **GfxRenderer**: Shows pages with fonts and themes
4. **EInkDisplay**: Final output with refresh mode control

### GfxRenderer Features

- **Render modes**: BW (1-bit), Grayscale LSB, Grayscale MSB
- **Orientation**: Portrait, Landscape CW/CCW, Inverted
- **Word caching**: 512-entry hash cache for word widths that occur again
- **Row buffers**: Allocated before use to prevent allocation for each line

### Refresh Modes

- **Full** — Complete redraw, clears ghosting (no ghosting)
- **Partial** — Fast page turns (some ghosting)
- **Fast** — Animation, menus (more ghosting)

The "Pages Per Refresh" setting controls how frequently a full refresh occurs (1/5/10/15/30 pages).

---

## Image Rendering

EPUB images (JPEG/PNG/BMP) are converted to BMP and cached to the SD card. Data URIs are removed before parse to prevent OOM. See [images.md](images.md) for more data.

---

## UI System

Papyrix uses a view-based UI architecture with elements that you can use again and rendering that is driven by state.

### Directory Structure

```
src/ui/
├── Elements.h/cpp          # Reusable UI components
├── Views.h                 # Unified header for all views
└── views/                  # Screen-specific views
    ├── HomeView.h/cpp      # Home screen with book card
    ├── ReaderViews.h/cpp   # Reader UI (TOC, status bar)
    ├── SettingsViews.h/cpp # Settings screens
    ├── NetworkViews.h/cpp  # WiFi configuration
    ├── AppLauncherViews.h/cpp # App launcher menu
    ├── CalibreViews.h/cpp  # Calibre sync UI
    ├── UtilityViews.h/cpp  # Common elements
    └── BootSleepViews.h/cpp# Boot splash, sleep screen
```

### UI Elements

The `ui::` namespace gives rendering components that you can use again:

- **`ButtonBar`** — 4-button hint bar at the screen bottom
- **`title()`** — Centered bold heading
- **`menuItem()`** — Menu entry that you can select
- **`toggle()`** — On/Off setting row
- **`enumValue()`** — Setting with value display
- **`keyboard()`** — On-screen keyboard (10x10 grid)
- **`battery()`** — Battery icon with percentage
- **`bookCard()`** — Cover + title + author
- **`fileEntry()`** — File name with directory indicator
- **`chapterItem()`** — TOC entry with depth indent
- **`wifiEntry()`** — Network + signal + lock icon
- **`dialog()`** — Yes/No confirmation
- **`readerStatusBar()`** — Battery, title, page numbers (chapter page count is available only after cache)

### ButtonBar Pattern

Views use `ButtonBar` to set which buttons are active and their labels:

```cpp
ui::ButtonBar buttons("Back", "Select", "", "");  // 2 active buttons
ui::buttonBar(renderer, theme, buttons);
```

### View Pattern

Views are rendering functions with no state. States own the data and call views:

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

`tools/reader-test/` is a desktop tool that runs the full content parse pipeline (EPUB/FB2/HTML/TXT/Markdown) with no hardware. It uses the same built-in fonts and viewport dimensions as the device. Page boundaries are the same.

### Device Emulation

- **Real font metrics**: Uses `reader_2b`, `reader_bold_2b`, `reader_italic_2b` built-in fonts with `advanceX` lookup for each glyph (not a fixed-width approximation)
- **Device viewport** (X4 default): 464×765 pixels (480 − 2×(3+5) × 800 − 9 − (3+23)) with status bar, 464×788 with no status bar
- **X3 viewport**: 512×757 with status bar, 512×780 with no status bar. Build with `-DPAPYRIX_TEST_X3` to use X3 panel dimensions in the mock EInkDisplay.
- **Batched caching**: `--batch 5` copies the device batched page cache generation with suspend/resume cycles
- **Status bar toggle**: `--no-statusbar` removes the 23px bottom margin, matching the device viewport when the status bar is hidden
- **Font ID**: `READER_FONT_ID = 1818981670`, same as the device

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

The mock `GfxRenderer` gives real text measurement (`getTextWidth`, `getSpaceWidth`, `getLineHeight`, `getFontAscenderSize`, `breakWordWithHyphenation`) with the font map. All drawing methods do nothing.

### Usage

```bash
# Parse book with device-matching batch mode
reader-test --dump --batch 5 book.epub /tmp/cache

# Parse with status bar hidden (larger viewport)
reader-test --dump --no-statusbar book.epub /tmp/cache

# Dump text from device cache (copied from SD card)
reader-test --cache-dump /path/to/.papyrix/epub_<hash>/

# Compare to find text differences (missing/duplicated text)
diff <(reader-test --dump --batch 5 book.epub /tmp/cache 2>/dev/null) \
     <(reader-test --cache-dump /path/to/device-cache/ 2>/dev/null)
```

### Verifying Parser Fixes

To verify repairs to the parse/cache pipeline:

1. Build reader-test **with no** repair, run with `--batch 5`, save output
2. Apply the repair, build again, run again
3. Diff the outputs — recovered text confirms that the repair operates

The `--batch 5` flag is necessary to reproduce suspend/resume defects that only start at batch boundaries during page cache generation.

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
- **`ReaderState.h`** — One reader (largest state)
- **`HomeState.h`** — Primary hub with async cover load
- **`SettingsState.h`** — Preferences UI

### Content (`/src/content/`)

- **`ContentHandle.h`** — Tagged union for providers
- **`EpubProvider.h`** — EPUB format support
- **`Fb2Provider.h`** — FB2 (FictionBook 2.0) format support
- **`XtcProvider.h`** — XTC/XTCH format support
- **`TxtProvider.h`** — Plain text support
- **`MarkdownProvider.h`** — Markdown format support
- **`ProgressManager.h`** — Reading position persistence
- **`ReaderNavigation.h`** — Page/chapter movement

### UI (`/src/ui/`)

- **`Elements.h`** — UI components that you can use again (ButtonBar, keyboard, and more)
- **`Views.h`** — One header for all view types
- **`views/HomeView.h`** — Home screen rendering
- **`views/ReaderViews.h`** — Reader UI (TOC, status bar)
- **`views/SettingsViews.h`** — Settings screen rendering

### Libraries (`/lib/`)

- **`Epub/`** — EPUB parse, CSS, TOC
- **`Fb2/`** — FB2 (FictionBook 2.0) parse, metadata extraction, TOC
- **`Xtc/`** — XTC/XTCH native format
- **`Txt/`** — Plain text file handling
- **`Markdown/`** — Markdown format support
- **`PageCache/`** — One page cache
- **`GfxRenderer/`** — Graphics rendering
- **`EpdFont/`** — Font load (full and streaming modes) and glyph cache
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
