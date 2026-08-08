# Firmware Code Review — papyrix-reader

**Scope:** `./src` and `./lib` C/C++ sources
**Platform:** ESP32-C3 (Arduino, PlatformIO, C++20, `-Oz -flto`)
**Goals:** Safe firmware, low memory usage, fast book rendering
**Date:** 2026-08-07
**Revision:** 3 — verified after firmware safety hardening

---

## Verdict

The previously identified malformed-input and background-cache ownership problems are now hardened. PageCache validates headers, LUT spans, and page positions; nested Page payloads reject truncated framing; ZIP and BMP readers check structural bounds; settings and EPUB metadata caches decode transactionally; and Reader mode no longer performs SD operations after a background-task stop timeout.

No immediate critical memory-safety issue was found in the reviewed paths. Remaining work is mostly low-risk durability, maintainability, or measurement-driven performance work. The intentional Reader/UI restart and framebuffer-as-scratch design should remain unless the memory architecture changes.

---

## Fixed safety findings

### F1. Background cache ownership transfer

**Files:** `src/states/ReaderState.cpp`, `lib/AsyncTask/src/BackgroundTask.cpp`
**Status:** fixed

Interactive callers abort when `stopBackgroundCaching()` times out. Lifecycle paths wait for task completion before destroying task-owned resources. `BackgroundTask` does not destroy its event group while its task may still be running.

`exitToUI()` now also fails closed. If the bounded stop times out, it records a one-shot UI transition in RTC no-init memory and restarts without further SD, framebuffer, PageCache, or parser access. Boot-mode detection consumes the marker once before consulting SD-backed settings.

### F2. Generic serialization truncation

**File:** `lib/Serialization/src/Serialization.h`
**Status:** fixed

Checked POD and string writers are available. `readPodValidated()` reads into initialized temporary storage and leaves the destination unchanged on a short read.

### F3. PageCache header and LUT validation

**File:** `lib/PageCache/src/PageCache.cpp`
**Status:** fixed

Cache headers use one checked field-by-field decoder. Header size, LUT span, LUT entry positions, seeks, copies, writes, syncs, extend paths, and commit paths are validated. Valid page positions satisfy:

```text
kHeaderSize <= pagePosition < lutOffset
```

### F4. Nested Page payload serialization

**Files:**

- `lib/RenderTypes/src/Page.cpp`
- `lib/RenderTypes/src/blocks/TextBlock.cpp`
- `lib/RenderTypes/src/blocks/ImageBlock.cpp`

**Status:** fixed

Page counts, tags, coordinates, word data, styles, image paths, and image dimensions now use checked reads/writes. Truncated payloads return failure instead of using indeterminate scalar values, and short writes propagate to PageCache before LUT commit.

### F5. Settings decoding

**Files:**

- `src/core/SettingsSerialization.cpp`
- `src/core/PapyrixSettings.cpp`

**Status:** fixed

Both settings load APIs use one transactional decoder. Mandatory headers and every declared field are checked. Older files still preserve defaults for fields not present in their declared field count. Corrupt input does not partially update the live Settings object.

The format remains field-by-field. Raw `Settings` or `RenderConfig` struct serialization should not be introduced because padding, ABI, and `bool` representation are not stable persistence contracts.

### F6. Reader metrics index

**File:** `src/states/ReaderState.cpp`
**Status:** fixed

All RenderConfig fields and metrics entries are read with checked helpers. Entries are decoded into temporary storage and committed only after the complete index is valid.

### F7. EPUB metadata cache reads

**File:** `lib/Epub/src/Epub/BookMetadataCache.cpp`
**Status:** fixed

The cache header and metadata decode into local state. Spine/TOC counts are bounded, the LUT must begin at the expected position and fit in the file, and every LUT entry and seek is validated against the data region and file size. Malformed caches fail cleanly and can be rebuilt.

### F8. ZIP validation

**File:** `lib/ZipFile/src/ZipFile.cpp`
**Status:** fixed

EOCD and central-directory reads are checked. Entry counts, names, compressed/inflated sizes, file spans, seeks, allocation limits, decompression output, and `inflatedDataSize + 1` arithmetic are validated.

### F9. BMP and alignment bounds

**Files:**

- `lib/GfxRenderer/src/BitmapHelpers.cpp`
- `lib/GfxRenderer/src/Bitmap.cpp`
- `lib/RenderTypes/src/ParsedText.cpp`

**Status:** fixed

BMP scalar reads, dimensions, palette reads, and pixel spans are checked. Centered and right-aligned overlong text uses signed calculations and clamps offsets without unsigned wraparound.

### F10. Framebuffer and rendering hardening

**Files:** `lib/GfxRenderer/src/GfxRenderer.cpp`, `lib/GfxRenderer/src/BitmapHelpers.h`
**Status:** fixed

Framebuffer indices use `size_t`, debug assertions guard internal coordinate contracts, and public rendering paths clip inputs. Thai cluster rendering writes clipped pixels directly through oriented framebuffer access. BMP palettes are read incrementally rather than using a large temporary stack allocation.

---

## Remaining valid findings

### R1. ReaderState remains large

**File:** `src/states/ReaderState.cpp` — approximately 3191 lines
**Severity:** maintainability
**Priority:** low to medium

ReaderState still owns loading, navigation, rendering, caching, overlays, progress, bookmarks, and indexing. This increases review cost, but line count alone is not a correctness defect. Extract components only when modifying those responsibilities, rather than performing a broad rewrite.

A small cache-ownership wrapper could make future access discipline harder to violate, but current callers enforce the ownership transfer contract.

### R2. Repeated RenderConfig field lists

**Files:** `src/states/ReaderState.cpp`, `lib/PageCache/src/PageCache.cpp`
**Severity:** maintainability/durability
**Priority:** medium

RenderConfig persistence and comparison still repeat the field list. Introduce shared checked field-by-field encode/decode helpers when this format next changes. Do not serialize the raw struct.

### R3. Metadata and metrics writers still use unchecked convenience writers

**Files:** `lib/Epub/src/Epub/BookMetadataCache.cpp`, `src/states/ReaderState.cpp`
**Severity:** cache durability
**Priority:** low

Several disposable metadata-cache and metrics-index construction paths still use `writePod()`/`writeString()` rather than checked variants. A partial file is rejected on the next read and rebuilt, so this is not currently a malformed-input memory-safety hole. Propagating write/sync failures would avoid publishing work known to be incomplete.

### R4. Hot PageCache extension retains old LUT blocks

**File:** `lib/PageCache/src/PageCache.cpp`
**Severity:** SD usage/performance
**Priority:** low

Each hot extension appends pages and a new LUT after the previous LUT. Old LUT blocks become unreachable interior data. Accumulation can exceed four bytes per final page across repeated extensions. Measure real cache growth before redesigning or compacting the file format.

### R5. Width-cache full reset

**File:** `lib/GfxRenderer/src/GfxRenderer.cpp`
**Severity:** performance
**Priority:** low

When the 256-entry width cache fills, all keys are cleared. This may cause a re-measurement spike, but linear probing prevents incorrect collision results. Benchmark layout latency before adding LRU bookkeeping and RAM overhead.

### R6. Float Knuth–Plass calculations

**File:** `lib/RenderTypes/src/ParsedText.cpp`
**Severity:** performance
**Priority:** measurement required

The ESP32-C3 has no hardware FPU, and line-breaking badness/demerits use `float` in an O(n²) dynamic program. This is a plausible hotspot, not a demonstrated defect. Profile long paragraphs before replacing it with fixed-point or a simpler optional breaker.

### R7. `int16_t` width-cache values

**File:** `lib/GfxRenderer/src/GfxRenderer.h`
**Severity:** latent range limit
**Priority:** no current fix

Widths above 32767 would truncate, but current supported viewports cannot reach that range. Expanding values to `int32_t` costs about 512 bytes of constrained RAM and is not justified today.

---

## Intentional designs — do not change without new evidence

### Reader/UI restart boundary

`exitToUI()` uses `ESP.restart()` intentionally. UI and Reader are separate boot modes, allowing Reader mode to reclaim memory held by UI state, fonts, Wi-Fi, and caches. Replacing this with a direct Home transition would undermine the memory model and reintroduce teardown pressure.

### Framebuffer scratch reuse

Page construction uses the framebuffer as a bounded BuildArena while rendering is stopped. This saves a large transient allocation on a device with limited contiguous heap. Keep the ownership rule documented and tested; do not allocate a second scratch arena solely for type purity.

### Single framebuffer

`platformio.ini` defines:

```text
EINK_DISPLAY_SINGLE_BUFFER_MODE=1
```

Therefore only one maximum-size static framebuffer is compiled, approximately 52 KB—not approximately 104 KB. Double-buffer memory analysis does not apply to the current firmware configuration.

### Greedy and optimized rendering paths

Per-pixel release checks, generalized cache eviction, and more complex line-breaking algorithms all carry speed or RAM costs. Existing clipping and debug assertions are appropriate unless device measurements demonstrate a reachable failure.

---

## Recommended next work

1. Test the hardened firmware on device with large EPUBs, rapid navigation, menu/TOC transitions, and intentionally truncated caches.
2. Check metadata/metrics writer and sync results so disposable caches are never knowingly published incomplete.
3. Centralize checked RenderConfig field serialization when its schema next changes.
4. Measure PageCache stale-LUT growth on large books.
5. Profile long-paragraph layout and width-cache reset latency before performance changes.
6. Extract ReaderState responsibilities incrementally only when feature work touches them.

---

## Verification

The hardening changes include regression coverage for truncated serialization, PageCache headers/LUTs, nested Page payloads, EPUB metadata LUTs, ZIP directories, BMP input, settings files, Reader metrics, alignment overflow, and RTC emergency transitions.

Required verification before merge:

```bash
make format
make test
make build
make check
git diff --check
```
