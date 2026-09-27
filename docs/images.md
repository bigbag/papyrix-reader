# Image Rendering

PapyriX converts supported EPUB images for the active display.

## Pipeline

```
EPUB HTML → ChapterHtmlSlimParser → ImageConverter → BMP Cache → GfxRenderer
```

1. **HTML Parsing**: Finds `<img>` tags. Gets `src` and `alt` attributes.
2. **Data URI Stripping**: Removes embedded base64 images before XML parse (prevents OOM).
3. **Image Extraction**: Gets the image from the EPUB ZIP to a temporary file.
4. **Conversion**: Converts JPEG/PNG to BMP format.
5. **Caching**: Stores the converted BMP on the SD card.
6. **Rendering**: Shows the image in the center of the page.

---

## Supported Formats

- **JPEG** (`.jpg`, `.jpeg`) — Baseline only (see below)
- **PNG** (`.png`) — Transparency shown as opaque
- **BMP** (`.bmp`) — Direct display. No conversion is necessary.

Format detection is case-insensitive.

### JPEG Encoding Support

The picojpeg decoder supports:
- **Baseline DCT** (SOF0) — Standard one-pass JPEG
- **Extended sequential DCT** (SOF1) — Extended baseline

**Not supported** (shown as a placeholder):
- **Progressive DCT** (SOF2) — Multi-pass progressive JPEG
- **Arithmetic coding** (SOF9, SOF10) — Not used frequently

The firmware finds progressive JPEGs. It scans for SOF markers before decode. If it finds one, it skips the image and shows a placeholder.

To convert progressive JPEGs to baseline, use tools such as ImageMagick:
```bash
convert progressive.jpg -interlace none baseline.jpg
```

---

## Size Constraints

- **Max parse width**: 2048px — Memory limit during decode
- **Max parse height**: 3072px — Memory limit during decode
- **Max render height**: viewport — Images taller than half the viewport get one page
- **Min dimension**: 20px — Images with width or height less than 20px are skipped. They are decorative.
- **Min free heap**: 8KB — Parse stops if memory goes below this

Images that are wider than the viewport are scaled down. Aspect ratio stays the same.

---

## When Images Are Rendered

Images are shown when all these conditions are true:
- The `showImages` setting is on
- The source path is valid and not empty
- The source is not a data URI
- The format is supported (JPEG/PNG/BMP)
- The file is in the EPUB archive
- Conversion completes
- Sufficient memory is available (8KB free or more)
- Fewer than 3 failures one after the other in the current chapter

---

## When Images Are Skipped

### Silently skipped (no placeholder)

- **Unsupported format** — Not JPEG/PNG/BMP (for example GIF, SVG, WebP, TIFF). Found by file extension before processing.
- **Tiny decorative images** — Width or height less than 20px (for example, 1px-tall JPEG line separators, small spacer PNGs, decorative borders). These are not visible on e-paper. They only use vertical space.

### Skipped with placeholder text `[Image: alt-text]`

- **`showImages` disabled** — User preference
- **Empty/malformed source** — Invalid HTML
- **Data URI source** — Memory protection (see below)
- **Progressive/arithmetic JPEG** — picojpeg limitation
- **File not found** — Missing from the EPUB archive
- **Conversion failure** — Damaged file or I/O error
- **Insufficient memory** — Less than 8KB free heap
- **Failure rate limit** — 3 or more failures one after the other

---

## Data URI Handling

Some EPUBs put images in as base64 data URIs:

```html
<img src="data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEASABIAAD..." />
```

`DataUriStripper` removes image data URIs before XML parsing.
It replaces each matching `src` value with `#`.
It accepts single quotes and double quotes.
It matches the prefix without case sensitivity.
It handles values that cross input buffer boundaries.
The XML parser does not allocate the embedded image data.

### Key Files

- `lib/Epub/src/Epub/parsers/DataUriStripper.h`

---

## Image Caching

### Cache Location

Images use `<device-cache>/epub_<hash>/images/` on the SD card.
The [device cache root](device-specifications.md#storage) depends on the device.

```
<device-cache>/
└── epub_12345678/
    └── images/
        ├── a1b2c3d4.bmp      # Converted image
        ├── e5f6g7h8.bmp      # Another converted image
        └── i9j0k1l2.failed   # Failed conversion marker
```

### Filename Generation

Cache filenames use an FNV-1a hash of the resolved image path:
- Input: Full path in the EPUB (for example, `OEBPS/images/cover.jpg`)
- Output: 8-character hex hash (for example, `a1b2c3d4.bmp`)

This makes sure:
- The same image that is referred to more than one time is cached one time
- No path character escaping is necessary
- Filenames have a fixed length

### Failed Conversion Markers

When image conversion fails, a `.failed` marker file is created:
- Prevents a new conversion try on later loads
- Contains no data (empty file)
- Cleared when the book cache is cleared

---

## Failure Rate Limiting

To prevent a damaged EPUB from causing long delays, image processing uses a failure rate limit:

- **Threshold**: 3 failures one after the other
- **Scope**: For each chapter (resets when you go to a new spine item)
- **Behavior**: After the threshold, remaining images in the chapter show as placeholders

This makes sure that some damaged images do not prevent you from reading the remainder of the chapter.

---

## Memory Management

### Heap and Stack Checks

The converter checks the lowest recorded free stack of the current task. It stops below 4096 bytes on ESP32-S3 or below 1024 bytes on ESP32-C3. BMP conversion also checks row-buffer memory when it needs more than 1024 bytes. It stops if the estimated buffers need more than 80% of the largest free 8-bit heap block.

The ESP32-S3 reader and cache tasks each use a 20 KiB stack. The ESP32-C3 reader and cache tasks each use a 12 KiB stack.

### Temporary Files

Image extraction uses a temporary file on the SD card:
1. Extract from ZIP to a temporary file.
2. Convert the temporary file to BMP.
3. Delete the temporary file.
4. Cache the BMP result.

This prevents the full source image from staying in RAM.

---

## Settings

### Show Images

**Settings > Reader > Show Images**

- **On** (default): Images are shown in the text
- **Off**: All images show as `[Image: alt-text]` placeholders

If you set images to off:
- Memory use decreases
- Page rendering is faster
- This is useful for reading that is mostly text

---

## Troubleshooting

### Images Not Displaying

1. Enable **Settings > Reader > Show Images**.
2. Check that the image uses JPEG, PNG, or BMP format.
3. Check the free space on the SD card.
4. Clear the book cache (**Settings > Cleanup > Clear Book Cache**) after a conversion failure. A `.failed` marker prevents automatic retry.

### Slow Page Loading with Images

1. The first load converts images (slower).
2. Later loads use the cache (faster).
3. You can set images to off for faster reading.

### Out of Memory Errors

1. Large images can go above available RAM.
2. Try a different EPUB with smaller images.
3. Use [xteink-epub-optimizer](https://github.com/bigbag/xteink-epub-optimizer) to change the image size.
