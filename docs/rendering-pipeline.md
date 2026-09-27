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
Split-spine anchors use chapter-wide page indexes.
See [file formats](file-formats.md) for record layouts.

## Cache Scheduling

Full Book Process indexes the book before reading.
The user can cancel with Back.
XTC and XTCH do not require text layout.

Foreground caching handles a requested page that is not yet available.
Background caching extends the cache while the user reads.
Cancellation uses `AbortCallback`.
The background task deletes itself after it stops.

The background task owns the parser and page cache while it runs.
The main thread accesses them only after the task stops.
Do not force-delete the task or access its resources concurrently.

## Page Totals

A partial cache contains an incomplete page count.
The status bar adds `~` to an estimated total.
The total becomes exact after indexing completes.
See the [status bar reference](user_guide.md#status-bar).

## Source Files

- `lib/GfxRenderer/src/GfxRenderer.h`
- `lib/EpdFont/src/EpdFont.h`
- `lib/EpdFont/src/StreamingEpdFont.h`
- `lib/ExternalFont/src/ExternalFont.h`
- `lib/PageCache/src/PageCache.cpp`
- `src/states/ReaderState.cpp`
- `src/FontManager.cpp`
