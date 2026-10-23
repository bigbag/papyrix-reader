# File Formats

PapyriX stores EPUB, TXT, Markdown, FB2, and HTML page caches in binary files.

`<device-cache>` is the device-specific cache directory listed in
[Device Specifications](device-specifications.md#storage).

Cover conversion keeps the aspect ratio. It reduces covers to 528×792 on X3
or 480×800 on X4, X4 Pro, X4 v2 Classic, and Sticky.
It does not enlarge smaller covers.

## Reading History

`/.papyrix/reading-stats.bin` stores up to 64 statistics records. `/.papyrix/recent.bin` stores up to ten recent books.

If a history file is missing, PapyriX starts an empty history. A read, data, or memory error leaves the saved file unchanged. PapyriX retries the load before the next change. A failed reload keeps the last valid recent-books list in memory.

The stores check temporary-file writes and synchronization before replacement. Replacement is not power-loss atomic.

## Reading Progress

`ProgressManager` saves `progress.bin` for all supported content types.
Current records use 14 bytes with little-endian integers.

| Offset | Size | Field |
|---|---|---|
| `0x00` | 4 bytes | Magic `0x47525050` (`PPRG`) |
| `0x04` | 1 byte | Version `1` |
| `0x05` | 1 byte | `ContentType` value |
| `0x06` | 4 bytes | Signed spine or section index |
| `0x0A` | 4 bytes | Unsigned page number |

EPUB and FB2 use the index and a page within that section.
XTC uses a flat page number.
TXT, Markdown, and HTML use the page number without the index.
The loader also accepts legacy 4-byte records.
New saves use the current format.

## TXT Cache Files

TXT files use a simple cache format in `<device-cache>/txt_<hash>/`.

### `progress.bin`

Uses the shared [reading progress format](#reading-progress).

### `index.bin`

Stores the page index. These are byte offsets where each page starts in the source file. The index is not valid and is built again when file size, viewport width, or lines-per-page changes.

```
Offset  Size        Description
0x00    4           File size (uint32_t) - for cache validation
0x04    4           Viewport width (int32_t) - for cache validation
0x08    4           Lines per page (int32_t) - for cache validation
0x0C    4           Page count (uint32_t)
0x10    4 * count   Page offsets (uint32_t[]) - byte offset in file where each page starts
```

### `cover.bmp`

Optional cover image. Found by a search for (case-insensitive):
1. `<filename>.jpg`, `<filename>.jpeg`, `<filename>.png`, or `<filename>.bmp` (matching the TXT filename)
2. `cover.jpg`, `cover.jpeg`, `cover.png`, or `cover.bmp` in the same directory

The image is converted to 1-bit dithered BMP format for display.

---

## Markdown Cache Files

Markdown files (`.md`, `.markdown`) use a cache format in `<device-cache>/md_<hash>/`.

### `progress.bin`

Uses the shared [reading progress format](#reading-progress).

### `section.bin`

Stores the parsed pages after layout. The format is almost the same as EPUB section files, but the header is simpler.

#### Version 1

```
Offset  Size        Description
0x00    1           Version (uint8_t) - version 1
0x01    4           Font ID (int32_t)
0x05    4           Line compression (float)
0x09    1           Indent level (uint8_t)
0x0A    1           Spacing level (uint8_t)
0x0B    1           Paragraph alignment (uint8_t)
0x0C    1           Hyphenation enabled (bool)
0x0D    1           Show images (bool)
0x0E    2           Viewport width (uint16_t)
0x10    2           Viewport height (uint16_t)
0x12    2           Page count (uint16_t)
0x14    4           LUT offset (uint32_t)
0x18    ...         Page data (same format as EPUB sections)
LUT     4 * count   Page offsets (uint32_t[])
```

The page data uses the same `Page` structure as EPUB section files (see below).

### `cover.bmp`

Optional cover image. Found by a search for (case-insensitive):
1. `<filename>.jpg`, `<filename>.jpeg`, `<filename>.png`, or `<filename>.bmp` (matching the Markdown filename)
2. `cover.jpg`, `cover.jpeg`, `cover.png`, or `cover.bmp` in the same directory

The image is converted to 1-bit dithered BMP format for display.

### Supported Markdown Features

- **Headers** (`#`, `##`, and more) - Shown bold and centered
- **Bold** (`**text**`) - Bold font style
- **Italic** (`*text*`) - Italic font style
- **Lists** (`-`, `*`, `1.`) - Bulleted with `•` prefix
- **Blockquotes** (`>`) - Shown in italic
- **Inline code** (`` `code` ``) - Shown in italic
- **Horizontal rules** (`---`) - Shown as a centered line
- **Links** - Text shown (URL not shown)
- **Images** - Placeholder `[Image]` shown
- **Code blocks** - Placeholder `[Code: ...]` shown
- **Tables** - Placeholder `[Table omitted]` shown

---

## FB2 Cache Files

FB2 (FictionBook 2.0) files use a cache format in `<device-cache>/fb2_<hash>/`.

### `meta.bin`

Caches parsed metadata (title, author, cover reference, TOC). This prevents a new parse of the full XML on later loads. Not valid after version changes.

```
Offset  Size        Description
0x00    1           Version (uint8_t) — version 2
0x01    4+N         Title (length-prefixed UTF-8 string)
...     4+N         Author (length-prefixed UTF-8 string)
...     4+N         Cover path (length-prefixed UTF-8 string)
...     4           File size (uint32_t)
...     2           Section count (uint16_t)
...     2           TOC item count (uint16_t)
...     [repeating] TOC items:
          4+N         Title (length-prefixed UTF-8 string)
          2           Section index (int16_t, 0-based)
```

### `progress.bin`

Uses the shared [reading progress format](#reading-progress).

### `sections/`

Cached chapter pages, same format as EPUB section files. Files named by section index (`0.bin`, `1.bin`, and more).

### `cover.bmp`

Optional cover image. Found by a search for (case-insensitive):
1. `<filename>.jpg`, `<filename>.jpeg`, `<filename>.png`, or `<filename>.bmp` (matching the FB2 filename)
2. `cover.jpg`, `cover.jpeg`, `cover.png`, or `cover.bmp` in the same directory

The image is converted to 1-bit dithered BMP format for display.

### Supported FB2 Features

- **Metadata** — Title from `<book-title>`, author from `<first-name>` + `<last-name>` (from `<title-info>` only)
- **Sections** — `<section>` elements used as chapters with page breaks between them
- **Titles/Subtitles** — Shown bold and centered
- **Paragraphs** (`<p>`) — Standard paragraph layout with alignment that you can set
- **Emphasis** (`<emphasis>`) — Italic font style
- **Code** (`<code>`) — Italic font style (no monospace font is included; same as `<emphasis>`)
- **Strong** (`<strong>`) — Bold font style
- **Empty lines** (`<empty-line>`) — Vertical spacing
- **Poetry** (`<poem>`, `<stanza>`, `<v>`) — Verse lines shown left-aligned with spacing between stanzas
- **Text attribution** (`<text-author>`) — Author attribution lines
- **Quotations** (`<cite>`) — Block quotations with visual spacing
- **Epigraphs** (`<epigraph>`) — Epigraph blocks with visual spacing
- **Annotations** (`<annotation>`) — Annotation blocks with visual spacing
- **TOC navigation** — Built from section titles. Supports jump to sections
- **RTL detection** — Arabic text found from the first chunk. Enables RTL layout
- **Namespace handling** — Removes XML namespace prefixes for compatibility
- **Binary skip** — `<binary>` tags (embedded images) skipped to save memory
- **Images** — Not supported (inline images are skipped)

---

## HTML Cache Files

HTML files (`.html`, `.htm`) use a cache format in `<device-cache>/html_<hash>/`. HTML uses the same parse pipeline as EPUB chapters (HTML5 normalization → Expat XML → page layout).

### `progress.bin`

Uses the shared [reading progress format](#reading-progress).

### `pages_<fontId>.bin`

Stores the parsed pages after layout (same section format as Markdown and FB2). The filename includes the font ID hash. The cache is not valid when the font changes.

### `cover.bmp`

Optional cover image. Found by a search for (case-insensitive):
1. `<filename>.jpg`, `<filename>.jpeg`, `<filename>.png`, or `<filename>.bmp` (matching the HTML filename)
2. `cover.jpg`, `cover.jpeg`, `cover.png`, or `cover.bmp` in the same directory

The image is converted to 1-bit dithered BMP format for display.

### Supported HTML Features

- **Title** — From the `<title>` tag (uses filename if there is no title)
- **Headings** (`<h1>`–`<h6>`) — Shown bold and centered
- **Paragraphs** (`<p>`) — Standard paragraph layout with alignment that you can set
- **Bold** (`<b>`, `<strong>`) — Bold font style
- **Italic** (`<i>`, `<em>`, `<code>`, `<tt>`, `<kbd>`, `<samp>`) — Italic font style (no monospace font is included; code-like inline tags show italic)
- **Preformatted** (`<pre>`) — Block element, forced left-align. Line breaks stay. Tabs expand to 4 spaces. Runs of spaces stay through non-breaking spaces
- **List items** (`<li>`) — Shown as block elements (list containers `<ul>`/`<ol>` are used as inline)
- **Blockquotes** (`<blockquote>`) — Indented block style
- **Divs** (`<div>`) — Block-level grouping
- **Line breaks** (`<br>`) — Inline line breaks
- **HTML entities** — Standard entities (`&amp;`, `&lt;`, `&gt;`, numeric character references)
- **Images** — Not supported (standalone HTML, no image extraction)

---

## EPUB Cache Files

EPUB files use a more complex cache format in `<device-cache>/epub_<hash>/`.

### Image Cache (`images/` subdirectory)

Inline images are converted to BMP and cached:
- `<hash>.bmp` — Converted image (FNV-1a hash of resolved image path)
- `<hash>.failed` — Marker file for failed conversions (prevents new tries)

## `book.bin`

### Version 7

ImHex Pattern:

```c++
import std.mem;
import std.string;
import std.core;

// === Configuration ===
#define EXPECTED_VERSION 7
#define MAX_STRING_LENGTH 65535

// === String Structure ===

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

// === Metadata Structure ===

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language tag")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
} [[comment("Book metadata information")]];

// === Spine Entry Structure ===

struct SpineEntry {
    String href [[comment("Resource path")]];
    s16 tocIndex [[comment("Index into TOC (-1 if none)"), color("4ECDC4")]];
} [[comment("Spine entry defining reading order")]];

// === TOC Entry Structure ===

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level (0-255)"), color("95E1D3")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)"), color("F38181")]];
} [[comment("Table of contents entry")]];

// === Book Bin Structure ===

struct BookBin {
    // Header
    u8 version [[comment("Format version"), color("FFD93D")]];

    // Version validation
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables"), color("6BCB77")]];
    u16 spineCount [[comment("Number of spine entries"), color("4D96FF")]];
    u16 tocCount [[comment("Number of TOC entries"), color("FF6B9D")]];

    // Metadata section
    Metadata metadata [[comment("Book metadata")]];

    // Validate LUT offset alignment
    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    // Lookup Tables
    u32 spineLut[spineCount] [[comment("Spine entry offsets"), color("4D96FF")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets"), color("FF6B9D")]];

    // Data Entries
    SpineEntry spines[spineCount] [[comment("Spine entries (reading order)")]];
    TocEntry toc[tocCount] [[comment("Table of contents entries")]];
};

// === File Parsing ===

BookBin book @ 0x00;

// Validate we've consumed the entire file
u32 fileSize = std::mem::size();
u32 parsedSize = $;

if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

### Related Files

- `toc.retry` — One-byte recovery state. Values from `0` to `3` record the number of recovery attempts. `0xFF` means no declared table of contents.
- `book.bin.new` — Replacement metadata in the `book.bin` format.
- `book.bin.bak` — Previous metadata in the `book.bin` format.

## `section.bin`

### Version 25

PapyriX rebuilds section caches with an unsupported format version when it opens the book.

The page-count index (`metrics.bin`) uses version 6. Anchor maps (`.anchors`) use version 3. PapyriX rejects indexes with an unsupported version before it uses their page counts or anchor positions. It rebuilds a section cache if its anchor map is missing or has an invalid header. A valid empty anchor map does not cause a rebuild.

ImHex Pattern:

```c++
import std.mem;
import std.string;
import std.core;

// === Configuration ===
#define EXPECTED_VERSION 25
#define MAX_STRING_LENGTH 65535

// === String Structure ===

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

// === Page Structure ===

enum StorageType : u8 {
    PageLine = 1,
    PageImage = 2
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3
};

enum BlockStyle : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
};

struct PageLine {
  s16 xPos;
  s16 yPos;
  u16 wordCount;
  String words[wordCount];
  u16 wordXPos[wordCount];
  WordStyle wordStyle[wordCount];
  BlockStyle blockStyle;
};

struct PageImage {
  s16 xPos;
  s16 yPos;
  String cachedBmpPath;  // Path to cached BMP on SD card
  u16 width;
  u16 height;
};

struct PageElement {
    u8 pageElementType;
    if (pageElementType == 1) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == 2) {
        PageImage pageImage [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];
};

// === Section Bin Structure ===

struct SectionBin {
    // Header
    u8 version [[comment("Format version"), color("FFD93D")]];

    // Version validation
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    // Cache busting parameters
    s32 fontId;
    float lineCompression;
    u8 indentLevel;
    u8 spacingLevel;
    u8 paragraphAlignment;
    bool hyphenation;
    bool showImages;
    u16 viewportWidth;
    u16 viewportHeight;
    u32 pageCount;
    u8 isPartial;
    u32 lutOffset;
    u32 bytesConsumed;
    u32 totalBytes;
    u32 sourceFingerprint;
    u32 fontFingerprint;

    Page page[pageCount];

    // Validate LUT offset alignment
    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    // Lookup Tables
    u32 lut[pageCount];
};

// === File Parsing ===

SectionBin book @ 0x00;

// Validate we've consumed the entire file
u32 fileSize = std::mem::size();
u32 parsedSize = $;

if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

---

## Bookmarks

### `bookmarks.bin`

Stores saved bookmarks. Present in all cache directories (EPUB, FB2, HTML, TXT, Markdown, XTC).

```
Offset  Size             Description
0x00    1                Bookmark count (uint8_t, firmware maximum 50)
0x01    72 * count       Bookmark entries
```

Each bookmark entry (72 bytes):

```
Offset  Size  Description
0x00    2     Spine index (int16_t) — chapter index for EPUB/FB2, unused for other formats
0x02    2     Section page (int16_t) — page in the chapter/section
0x04    4     Flat page (uint32_t) — absolute page number (used by TXT/Markdown/XTC formats)
0x08    64    Label (char[64]) — null-terminated bookmark title
```

Firmware that supports a maximum of 20 bookmarks loads only the first 20 entries. If this firmware saves the file, the save operation deletes the other entries.

### `bookmarks.txt`

Human-readable companion file exported with `bookmarks.bin`. Contains one line for each bookmark with the label and page position. This file is for user reference only. The firmware does not read it again.
