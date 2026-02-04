# Image Rendering

This document describes how Papyrix handles images in EPUB content.

## Pipeline

```
EPUB HTML → ChapterHtmlSlimParser → ImageConverter → BMP Cache → GfxRenderer
```

1. **HTML Parsing**: Detects `<img>` tags, extracts `src` and `alt` attributes
2. **Data URI Stripping**: Removes embedded base64 images before XML parsing (prevents OOM)
3. **Image Extraction**: Extracts image from EPUB ZIP to temp file
4. **Conversion**: Converts JPEG/PNG to BMP format
5. **Caching**: Stores converted BMP on SD card
6. **Rendering**: Displays image centered on page

---

## Supported Formats

- **JPEG** (`.jpg`, `.jpeg`) — Baseline only (see below)
- **PNG** (`.png`) — Transparency rendered as opaque
- **BMP** (`.bmp`) — Direct display, no conversion needed

Format detection is case-insensitive.

### JPEG Encoding Support

The picojpeg decoder supports:
- **Baseline DCT** (SOF0) — Standard single-pass JPEG
- **Extended sequential DCT** (SOF1) — Extended baseline

**Not supported** (displays as placeholder):
- **Progressive DCT** (SOF2) — Multi-pass progressive JPEG
- **Arithmetic coding** (SOF9, SOF10) — Rarely used

Progressive JPEGs are detected by scanning for SOF markers before decoding. If detected, the image is skipped and shows a placeholder instead.

To convert progressive JPEGs to baseline, use tools like ImageMagick:
```bash
convert progressive.jpg -interlace none baseline.jpg
```

---

## Size Constraints

- **Max parse width**: 2048px — Memory limit during decoding
- **Max parse height**: 3072px — Memory limit during decoding
- **Max render height**: viewport/2 — Prevents images dominating pages
- **Min free heap**: 8KB — Parsing aborts if memory drops below

Images exceeding viewport width are scaled down proportionally while maintaining aspect ratio.

---

## When Images Are Rendered

Images are rendered when all conditions are met:
- `showImages` setting is enabled
- Source path is valid and non-empty
- Source is not a data URI
- Format is supported (JPEG/PNG/BMP)
- File exists in EPUB archive
- Conversion succeeds
- Sufficient memory available (≥8KB free)
- Fewer than 3 consecutive failures in current chapter

---

## When Images Are Skipped

Images display as placeholder text `[Image: alt-text]` when:

- **`showImages` disabled** — User preference
- **Empty/malformed source** — Invalid HTML
- **Data URI source** — Memory protection (see below)
- **Unsupported format** — Not JPEG/PNG/BMP
- **Progressive/arithmetic JPEG** — picojpeg limitation
- **File not found** — Missing from EPUB archive
- **Conversion failure** — Corrupt file or I/O error
- **Insufficient memory** — <8KB free heap
- **Failure rate limit** — ≥3 consecutive failures

---

## Data URI Handling

### The Problem

Some EPUBs embed images as base64 data URIs:

```html
<img src="data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEASABIAAD..." />
```

These can be 1MB+ of text and cause out-of-memory crashes during XML parsing. The expat XML parser must allocate memory to store the entire attribute value.

### The Solution

The `DataUriStripper` pre-processes HTML buffers before the XML parser sees them:

1. Scans for `src="data:` patterns (case-insensitive, handles single/double quotes)
2. Replaces the data URI with `src="#"` in-place
3. Handles patterns that span buffer boundaries (streaming-safe)

This prevents memory allocation for embedded image data while preserving the document structure.

### Key Files

- `lib/Epub/Epub/parsers/DataUriStripper.h` — Header with interface
- `lib/Epub/Epub/parsers/DataUriStripper.cpp` — Implementation

---

## Image Caching

### Cache Location

Images are cached to SD card under `/.papyrix/epub_<hash>/images/`:

```
.papyrix/
└── epub_12345678/
    └── images/
        ├── a1b2c3d4.bmp      # Converted image
        ├── e5f6g7h8.bmp      # Another converted image
        └── i9j0k1l2.failed   # Failed conversion marker
```

### Filename Generation

Cache filenames use FNV-1a hash of the resolved image path:
- Input: Full path within EPUB (e.g., `OEBPS/images/cover.jpg`)
- Output: 8-character hex hash (e.g., `a1b2c3d4.bmp`)

This ensures:
- Same image referenced multiple times is cached once
- No path character escaping needed
- Fixed-length filenames

### Failed Conversion Markers

When image conversion fails, a `.failed` marker file is created:
- Prevents re-attempting conversion on subsequent loads
- Contains no data (empty file)
- Cleared when book cache is cleared

---

## Failure Rate Limiting

To prevent a corrupt EPUB from causing excessive delays, image processing implements failure rate limiting:

- **Threshold**: 3 consecutive failures
- **Scope**: Per chapter (resets when moving to new spine item)
- **Behavior**: After threshold reached, remaining images in chapter display as placeholders

This ensures that a few corrupt images don't prevent reading the rest of the chapter.

---

## Memory Management

### Heap Monitoring

Before processing each image:
1. Check `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
2. If < 8KB, skip image and show placeholder
3. Log warning for diagnostics

### Temporary Files

Image extraction uses a temporary file on SD card:
1. Extract from ZIP to temp file
2. Convert temp file to BMP
3. Delete temp file
4. Cache BMP result

This avoids holding the entire source image in RAM.

---

## Settings

### Show Images

**Settings > Display > Show Images**

- **On** (default): Images are rendered inline
- **Off**: All images display as `[Image: alt-text]` placeholders

Disabling images:
- Reduces memory usage
- Speeds up page rendering
- Useful for text-heavy reading

---

## Troubleshooting

### Images Not Displaying

1. Check **Settings > Display > Show Images** is enabled
2. Verify image format is JPEG/PNG/BMP
3. Check SD card has free space for cache
4. Try clearing book cache (**Settings > Cleanup > Clear Book Cache**)

### Slow Page Loading with Images

1. First load converts images (slower)
2. Subsequent loads use cache (faster)
3. Consider disabling images for faster reading

### Out of Memory Errors

1. Large images may exceed available RAM
2. Try a different EPUB with smaller images
3. Use [xteink-epub-optimizer](https://github.com/bigbag/xteink-epub-optimizer) to resize images