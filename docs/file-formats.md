# File Formats

This document describes the binary cache formats used by Papyrix for EPUB, TXT, Markdown, and FB2 files.

## TXT Cache Files

TXT files use a simple cache format stored in `.papyrix/txt_<hash>/`.

### `progress.bin`

Stores the current reading position as a single 4-byte little-endian unsigned integer (page number).

```
Offset  Size  Description
0x00    4     Current page number (uint32_t, little-endian)
```

### `index.bin`

Stores the page index - byte offsets where each page starts in the source file. The index is invalidated and rebuilt when file size, viewport width, or lines-per-page changes.

```
Offset  Size        Description
0x00    4           File size (uint32_t) - for cache validation
0x04    4           Viewport width (int32_t) - for cache validation
0x08    4           Lines per page (int32_t) - for cache validation
0x0C    4           Page count (uint32_t)
0x10    4 * count   Page offsets (uint32_t[]) - byte offset in file where each page starts
```

### `cover.bmp`

Optional cover image, discovered by searching for (case-insensitive):
1. `<filename>.jpg`, `<filename>.jpeg`, `<filename>.png`, or `<filename>.bmp` (matching the TXT filename)
2. `cover.jpg`, `cover.jpeg`, `cover.png`, or `cover.bmp` in the same directory

The image is converted to 1-bit dithered BMP format for display.

---

## Markdown Cache Files

Markdown files (`.md`, `.markdown`) use a cache format stored in `.papyrix/md_<hash>/`.

### `progress.bin`

Stores the current reading position as a 2-byte little-endian unsigned integer (page number).

```
Offset  Size  Description
0x00    2     Current page number (uint16_t, little-endian)
```

### `section.bin`

Stores the parsed and laid-out pages. The format is similar to EPUB section files but with a simpler header.

#### Version 1

```
Offset  Size        Description
0x00    1           Version (uint8_t) - currently 1
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

Optional cover image, discovered by searching for (case-insensitive):
1. `<filename>.jpg`, `<filename>.jpeg`, `<filename>.png`, or `<filename>.bmp` (matching the Markdown filename)
2. `cover.jpg`, `cover.jpeg`, `cover.png`, or `cover.bmp` in the same directory

The image is converted to 1-bit dithered BMP format for display.

### Supported Markdown Features

- **Headers** (`#`, `##`, etc.) - Rendered bold and centered
- **Bold** (`**text**`) - Bold font style
- **Italic** (`*text*`) - Italic font style
- **Lists** (`-`, `*`, `1.`) - Bulleted with `•` prefix
- **Blockquotes** (`>`) - Rendered in italic
- **Inline code** (`` `code` ``) - Rendered in italic
- **Horizontal rules** (`---`) - Rendered as centered line
- **Links** - Text displayed (URL not shown)
- **Images** - Placeholder `[Image]` shown
- **Code blocks** - Placeholder `[Code: ...]` shown
- **Tables** - Placeholder `[Table omitted]` shown

---

## FB2 Cache Files

FB2 (FictionBook 2.0) files use a cache format stored in `.papyrix/fb2_<hash>/`.

### `meta.bin`

Caches parsed metadata (title, author, cover reference, TOC) to avoid re-parsing the full XML on subsequent loads. Invalidated by version changes.

```
Offset  Size        Description
0x00    1           Version (uint8_t) — currently 2
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

Stores the current reading position (same format as EPUB).

### `sections/`

Cached chapter pages, same format as EPUB section files. Files named by section index (`0.bin`, `1.bin`, etc.).

### `cover.bmp`

Optional cover image, discovered by searching for (case-insensitive):
1. `<filename>.jpg`, `<filename>.jpeg`, `<filename>.png`, or `<filename>.bmp` (matching the FB2 filename)
2. `cover.jpg`, `cover.jpeg`, `cover.png`, or `cover.bmp` in the same directory

The image is converted to 1-bit dithered BMP format for display.

### Supported FB2 Features

- **Metadata** — Title from `<book-title>`, author from `<first-name>` + `<last-name>` (from `<title-info>` only)
- **Sections** — `<section>` elements treated as chapters with page breaks between them
- **Titles/Subtitles** — Rendered bold and centered
- **Paragraphs** (`<p>`) — Standard paragraph layout with configurable alignment
- **Emphasis** (`<emphasis>`) — Italic font style
- **Strong** (`<strong>`) — Bold font style
- **Empty lines** (`<empty-line>`) — Vertical spacing
- **TOC navigation** — Built from section titles, supports jumping to sections
- **RTL detection** — Arabic text detected from first chunk, enables RTL layout
- **Namespace handling** — Strips XML namespace prefixes for compatibility
- **Binary skip** — `<binary>` tags (embedded images) skipped to save memory
- **Images** — Not supported (inline images are skipped)

---

## EPUB Cache Files

EPUB files use a more complex cache format stored in `.papyrix/epub_<hash>/`.

### Image Cache (`images/` subdirectory)

Inline images are converted to BMP and cached:
- `<hash>.bmp` — Converted image (FNV-1a hash of resolved image path)
- `<hash>.failed` — Marker file for failed conversions (prevents re-attempts)

## `book.bin`

### Version 3

ImHex Pattern:

```c++
import std.mem;
import std.string;
import std.core;

// === Configuration ===
#define EXPECTED_VERSION 3
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

## `section.bin`

### Version 15

ImHex Pattern:

```c++
import std.mem;
import std.string;
import std.core;

// === Configuration ===
#define EXPECTED_VERSION 15
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
    bool extraParagraphSpacing;
    u8 paragraphAlignment;
    bool hyphenation;
    u16 viewportWidth;
    u16 viewportHeight;
    u16 pageCount;
    u32 lutOffset;

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
