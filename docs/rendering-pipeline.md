# Rendering Pipeline

## Display Framebuffer

The release builds use single-buffer mode.
The C3 artifact reserves 52,272 bytes for the largest supported panel.
The X4 Pro and X4 v2 Classic artifacts allocate their 48,000-byte buffers in PSRAM.
Initialization fails if that allocation fails.

- **X3**
  - Native dimensions: 792 × 528
  - Active frame bytes: 52,272
- **X4**
  - Native dimensions: 800 × 480
  - Active frame bytes: 48,000
- **X4 Pro**
  - Native dimensions: 800 × 480
  - Active frame bytes: 48,000
- **X4 v2 Classic**
  - Native dimensions: 800 × 480
  - Active frame bytes: 48,000

`lib/EInkDisplay/include/Display.h` defines the buffer interface.
`lib/EInkDisplay/src/Display.cpp` allocates and initializes the buffer.

## Viewport

`GfxRenderer` gets the screen dimensions from the active display profile.
Reader margins and orientation determine the text viewport.
The status bar reduces the text height.
It uses the same framebuffer as the page.

- **X3**
  - Portrait viewport with status bar: 512 × 757
  - Without status bar: 512 × 780
- **X4, X4 Pro, and X4 v2 Classic**
  - Portrait viewport with status bar: 464 × 765
  - Without status bar: 464 × 788

## Fonts

Built-in font bitmaps remain in Flash.
The C3 `.epdfont` cache retains up to 192 glyph bitmaps in RAM.
On S3, a 96 KiB PSRAM slab retains up to 256 glyph bitmaps when space is available.
If the PSRAM allocation fails, S3 uses the 192-entry RAM cache.
Streaming `.epdfont` files keep metadata in RAM and read cache misses from SD.
Bold and italic variants load when text requires them.
CJK fonts use a bounded bitmap cache.
Actual font memory use depends on the font data and loaded styles.

See [fonts](fonts.md) for formats and fallback rules.

## Antialiasing

The reader renders a black-and-white page first.
For grayscale text, it renders the two grayscale masks into the same framebuffer.
The display driver sends each mask to the controller.
The controller applies its grayscale waveform.
The reader then renders black-and-white data again to restore the controller state.
On S3, the reader retains up to four current-page BMP files for one render turn.
It uses at most 256 KiB of PSRAM for their rows.
Images that do not fit use a separate bitmap object for each render pass.
The C3 reader opens each BMP for each render pass.
The status bar uses one-bit rendering.

Classic uses monochrome rendering when the panel has no supported grayscale waveform.
This applies to text and images without changing the stored antialiasing preference.
See the [Classic panel policy](x4-classic-specifications.md#display-controller-selection) for supported variants.

The display driver owns controller-specific RAM synchronization and refresh rules.
Do not reuse SSD1677 commands on UC8253, UC8279, or UC8179.

## Page Cache

The parser writes page records to the SD card.
EPUB and FB2 parsers use a separate 64 KiB PSRAM scratch buffer on S3 when space is available.
If that allocation fails, they use the framebuffer.
The C3 parsers always borrow the framebuffer.
The cache stores a lookup table of page offsets.
Viewport, font, hyphenation, and rendering settings determine cache compatibility.
A layout change invalidates incompatible pages.

The default cache chunk contains five pages.
A partial cache records that more content remains.
Cache extension adds page records and a replacement lookup table.
It updates the header after those writes complete.
The previous header remains valid if extension stops before that update.

EPUB caches chapters separately.
Markdown, FB2, HTML, and TXT use a page cache for the document.
EPUB partial and complete cache saves keep anchors in source order.
Each original EPUB spine item uses one XML stream with 4 KiB input blocks.
Large chapters use complete normalized XHTML files on the SD card.
The parser keeps paragraph and page state across input blocks.
It does not add XML wrappers or page breaks at byte boundaries.
Metadata, page caches, page-count indexes, and anchor maps rebuild through version checks.
See [file formats](file-formats.md) for record layouts.

EPUB metadata loads do not prepare external styles. Reloading metadata discards the previous manifest's stylesheet list. The first chapter that needs layout loads compatible parsed rules from `styles.bin`, or prepares all source stylesheets and saves the complete result. Complete page-cache reads do not load CSS. An empty rule cache is valid for a book without external styles. The cache keeps property flags, precedence, and relative units, so font and viewport changes reuse the parsed rules.

CSS preparation uses the chapter's existing scratch storage for ZIP extraction. Memory checks cover string growth, declaration work, and retained rules instead of a fixed 96 KiB threshold. The free-block check covers simultaneous declaration copies. The parser excludes surrounding whitespace from the copy budget. The limits remain 64 KiB per source stylesheet, 512 rules, and 256 bytes per selector. Failed or cancelled preparation does not emit new pages with incomplete styles. Failed preparation stops cache extension and keeps the committed page cache. A missing or invalid rule cache rebuilds from source. Page cache version 29, metrics version 10, and anchor version 7 invalidate older layouts without deleting progress or bookmarks.

## Cache Scheduling

Full Book Process indexes the book before reading.
The user can cancel with Back.
XTC and XTCH do not require text layout.

Full-book indexing creates a missing EPUB or FB2 chapter cache with exclusive access. This avoids read retries for a new cache. Existing caches keep the normal read retries. An empty file has no valid cache header, so indexing rebuilds it after an interruption.

Full-book indexing keeps section counts in the existing page-metrics array. A complete run saves `metrics.bin` once and keeps the counts for the first page. This avoids another scan of all chapter caches. A skipped or failed section keeps the total estimated. If the array does not fit in memory, the reader defers the index rebuild. A complete chapter alone does not make an EPUB or FB2 book total exact.

Foreground caching handles a requested page that is not yet available.
Backward navigation uses a compatible complete chapter cache without an indexing screen.
The reader uses the stored page count to select the last page.

The reader shows `Indexing` before foreground cache creation or extension, including extension before the current cache ends. It keeps the message through all preparation chunks. It restores page-button input after the next page or overlay finishes its display refresh. Page-button presses during this wait do not turn pages afterward. A held page button needs a release and a new press before navigation resumes. Completed taps from before the wait remain available. Back and Power events remain queued, but synchronous preparation delays their handling. Normal display refreshes outside this wait keep short taps.

Background caching extends the cache while the user reads.
Cancellation uses `AbortCallback`.
The background task deletes itself after it stops.
Cancellation after setup keeps a compatible cache that has already loaded.
The next page turn can keep the live parser instead of parsing the chapter again.
The live parser keeps its bounded text-layout window, buffered word, and block spacing across batch stops, including the final input block.
Layout windows can end inside a CJK source word.
The parser clears completed block styles before it continues after an image.
If memory is too low for layout, the parser keeps the chapter incomplete.
After a restart, the parser reads the chapter from the beginning and skips pages already in the cache.
This replay cost grows with the cached part of the chapter.
Page progress resets the parser timeout during replay.

The background task owns the parser and page cache while it runs.
The main thread accesses them only after the task stops.
Do not force-delete the task or access its resources concurrently.

## Page Totals

A partial cache contains an incomplete page count. The status bar adds `~` to an estimated total. The total is exact only when every section cache is complete.
See the [status bar reference](user_guide.md#status-bar).

## Source Files

- `lib/GfxRenderer/src/GfxRenderer.h`
- `lib/EpdFont/src/EpdFont.h`
- `lib/EpdFont/src/StreamingEpdFont.h`
- `lib/ExternalFont/src/ExternalFont.h`
- `lib/PageCache/src/PageCache.cpp`
- `src/states/ReaderState.cpp`
- `src/FontManager.cpp`
