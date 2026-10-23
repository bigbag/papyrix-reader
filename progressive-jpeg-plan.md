# S3 Progressive JPEG Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert progressive JPEGs on X4 Pro and Classic without adding decoder code to C3 firmware.

**Architecture:** Add a checked, one-shot decoder based on the inspected Witchhunt source. Add a separate `ProgressiveJpegImageConverter` behind the existing factory. Reuse the row scaler, ditherers, BMP headers, and temporary-file publication path.

**Tech Stack:** C++17, PlatformIO, ESP-IDF heap capabilities, SdFat, existing CMake host tests, Python test scripts.

**Spec:** [progressive-jpeg-design.md](progressive-jpeg-design.md). Read the approved specification before this plan.

## Global Constraints

- Enable the feature for X4 Pro and X4 v2 Classic only. Exclude the new decoder and converter from C3 firmware.
- Keep baseline JPEG routing and public `ImageFormat::Jpeg` unchanged. The approved scope extension checks baseline writes and corrects pixel-count overflow in both row scalers.
- Do not change existing image failure markers or cache versions. This feature applies to new conversions only.
- Use full-size decoding. Do not add reduced-size decoding, checkpoints, resume files, telemetry, or a new setting.
- Accept Huffman-coded SOF2 with 8-bit samples.
- Accept one-component grayscale or three-component YCbCr. The first component carries luma. Each sampling factor is from 1 through 4. Luma uses the maximum horizontal and vertical factors present in the frame.
- Source dimensions: from 20 by 20 through 2048 by 3072 pixels, inclusive.
- Compressed input: no larger than 512 KiB.
- At most 32 luma scans and 16 retained Huffman table definitions.
- Retain the factory's 4096-byte stack admission check and the existing 20 KiB allocation reserve.
- Allocate decoder workspace from `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. Do not fall back to internal RAM.
- A final buffer shorter than 512 bytes is valid. Reject a read shorter than its request after clamping that request to the remaining file bytes.
- Require the end-of-image marker, complete output rows, checked writes, and successful synchronization before publication.
- Existing completed output remains untouched before publication. Do not claim that `commitFile` replacement is power-loss atomic.
- Keep design and development notes outside `docs/`. Update production documentation only after the behavior exists.
- Use STE for new comments and documentation. Do not add comments that repeat clear code.
- Preserve the decoder license and attribution. Do not change the inspected repository under `tmp/`.

---

## File map and ownership

| Task | Files | Responsibility |
|---|---|---|
| 1 | New `lib/ProgressiveJpeg/library.json`, `lib/ProgressiveJpeg/src/ProgressiveJpeg.h`, `lib/ProgressiveJpeg/src/ProgressiveJpeg.cpp`, `lib/ProgressiveJpeg/LICENSE`, `lib/ProgressiveJpeg/ACKNOWLEDGEMENTS.md` | Checked file inspection, scan indexing, entropy decoding, grayscale bands |
| 1 | `lib/BoardSupport/include/TargetConfig.h:61-90` | `PAPYRIX_CAP_PROGRESSIVE_JPEG`: zero on C3, one on Pro and Classic |
| 1 | New `test/unit/progressivejpeg/ProgressiveJpegTest.cpp`; new `test/fixtures/progressive-jpeg/` | Pixel, boundary, malformed-input, and cancellation regressions |
| 1–3 | `test/CMakeLists.txt` | Real source linkage and explicit host target selection |
| 2 | New `lib/ImageConverter/src/ProgressiveJpegImageConverter.h` and `.cpp` | PSRAM admission, scaling, dithering, checked BMP output |
| 2 | `lib/ImageConverter/library.json` | Dependency on the local decoder and target configuration |
| 2 | New `test/unit/imageconverter/ProgressiveJpegConverterTest.cpp` | Converter output and resource failures |
| 3 | `lib/ImageConverter/src/ImageConverter.cpp:136-172,184-244` | S3 JPEG selection and checked output synchronization |
| 3 | New `test/unit/imageconverter/ProgressiveJpegFactoryTest.cpp` | Production routing, publication, failures, baseline compatibility |
| 3 | `test/unit/imageconverter/ImageConverterSignatureTest.cpp` | Remove mock-echo routing assertions; retain useful signature and failure coverage |
| 3 | `scripts/check_target_features.py`, `test/scripts/test_target_features.py` | Reject progressive symbols in C3 artifacts |
| 3 | `docs/images.md:20-43,47-53,80-88` | Actual target support, progressive limits, and cache behavior |

Line numbers identify the inspected source, not fixed edit anchors. Re-read files before editing. Use LSP references before changing an exported symbol. Keep unrelated working-tree changes, including `report.md`, out of implementation commits.

Tasks are ordered: decoder, converter, then factory integration. They share `test/CMakeLists.txt`; do not edit that file concurrently. Inline execution is the simplest option. If work is delegated, agree on the interfaces below and give build-file integration to one owner. Do not run builds or formatters while another worker changes their inputs.

## Shared interfaces

These are new APIs, not claims about existing code. Keep the decoder independent of `ImageConverter` and ESP heap allocation. Remove the upstream public `Decoder`, `DecodeStats`, `base`, and `scaleShift` interfaces. Keep private scan cursors needed for band decoding.

```cpp
namespace ProgressiveJpeg {
enum class Result : uint8_t {
  Ok, Unsupported, InvalidData, IoError, OutOfMemory, Aborted, Stopped
};

struct ComponentInfo {
  uint8_t id = 0;
  uint8_t horizontal = 1;
  uint8_t vertical = 1;
  uint8_t quantizer = 0;
};

struct ImageInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t componentCount = 0;
  uint8_t maxHorizontal = 1;
  uint8_t maxVertical = 1;
  ComponentInfo components[3]{};
};

using AbortCallback = bool (*)(void* user);
using BandCallback = bool (*)(void* user, uint16_t y, const uint8_t* gray,
                              uint16_t width, uint16_t rows, uint16_t stride);

struct DecodeOptions {
  uint8_t* workspace = nullptr;
  size_t workspaceSize = 0;
  AbortCallback shouldAbort = nullptr;
  void* abortUser = nullptr;
};

Result probe(FsFile& file, ImageInfo& info,
             AbortCallback shouldAbort = nullptr, void* abortUser = nullptr);
size_t workspaceBytes(const ImageInfo& info);
Result decode(FsFile& file, const ImageInfo& info, const DecodeOptions& options,
              BandCallback callback, void* user);
}
```

`probe` restores the original input position on every exit. `decode` consumes a whole file beginning at offset zero and also restores its entry position. A failed restoration returns `IoError`. The caller owns the file and workspace. `decode` compares every indexed frame field against `info` before writing coefficient memory. This avoids a second probe while retaining geometry validation. `workspaceBytes` validates its input fields and returns zero for invalid geometry.

The converter's interface is:

```cpp
class ProgressiveJpegImageConverter final : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output,
               const ImageConvertConfig& config) override;
  const char* formatName() const override { return "Progressive JPEG"; }
};
```

The factory uses a static converter instance, as it does for the existing converters. The converter stores no mutable per-call state in that instance.

## Task 1: Checked progressive decoder

**Consumes:** `FsFile`, target capabilities, existing host SD fault injection, and the inspected upstream source.

**Produces:** the `ProgressiveJpeg` API above, capability values, and `ProgressiveJpegTest`.

### 1. Import and establish a pixel baseline

- [ ] Copy the decoder algorithms from `tmp/witchhunt-reader/lib/ProgressiveJpeg/` at revision `e05065f6c3f16a12dad5bc1313046ae3f4f203f1`. Copy the MIT notice from that repository and preserve its decoder acknowledgements. Use PapyriX's `src/` library layout and this manifest:

```json
{
  "name": "ProgressiveJpeg",
  "version": "1.0.0",
  "build": {"srcDir": "src", "includeDir": "src"},
  "dependencies": {"BoardSupport": "*", "SDCardManager": "*"}
}
```

- [ ] Add `PAPYRIX_CAP_PROGRESSIVE_JPEG` in each existing target branch. Wrap all new decoder declarations and definitions with that capability. Remove the C++20 memory helper. Require caller-owned workspace; do not substitute another hidden allocator.

```cpp
#include <TargetConfig.h>
#if PAPYRIX_CAP_PROGRESSIVE_JPEG
// Decoder declarations or definitions belong inside this guard.
#endif
```

- [ ] Adapt the existing `Session`, `layoutFor`, `geometryFor`, `indexStep`, and `decodeBand` into one-shot decoding. Fix the output scale at zero. Remove checkpoint serialization and public stepping state. Use placement construction in the supplied aligned workspace and run its destructor before return. Keep the large state out of the task stack. Retain the full-size IDCT and band algorithms rather than rewrite them.
- [ ] Generate new fixture pairs in `test/fixtures/progressive-jpeg/`: `color-420.jpg` / `.y.pgm`, `gray.jpg` / `.y.pgm`, and `color-444-restart.jpg` / `.y.pgm`. Generate `baseline.jpg` for baseline integration checks. Use deterministic PGM/PPM source patterns made by the local generator, `cjpeg` for encoding, and `djpeg` for independent grayscale references. Do not use any images from Witchhunt. Record the generator and tool versions with the fixtures. Tests must not depend on `tmp/witchhunt-reader`.
- [ ] Add the decoder host target with `PAPYRIX_TARGET_X4PRO=1`, production `ProgressiveJpeg.cpp`, and existing test helpers. Pass the fixture directory with a target compile definition. Do not extract C++ functions from source text.
- [ ] Implement local test helpers with these exact interfaces in `ProgressiveJpegTest.cpp`:

```cpp
struct Decoded {
  ProgressiveJpeg::Result result;
  ProgressiveJpeg::ImageInfo info;
  std::vector<uint8_t> pixels;
};
std::string readFixture(const char* name);
std::vector<uint8_t> readPgmPixels(const char* name, int& width, int& height);
Decoded decodeBytes(const std::string& bytes,
                    ProgressiveJpeg::AbortCallback abort = nullptr,
                    void* abortUser = nullptr);
```

`readFixture` reads the binary fixture with `std::ifstream` and fails the test if it cannot read it. `readPgmPixels` parses P5, skips header comments, requires maximum value 255, then requires exactly width × height bytes. `decodeBytes` registers `/progressive.jpg` with `SdMan`, probes it, allocates the reported workspace, and calls production `decode`. Its band callback rejects out-of-order rows, wrong width, or a stride smaller than width; it appends only the visible width of each row. It does not turn a decoder error into success.

- [ ] Compare every decoded pixel, not just an output length or checksum generated by the same decoder. Apply this assertion to each fixture:

```cpp
const auto decoded = decodeBytes(readFixture("color-420.jpg"));
int width = 0, height = 0;
const auto expected = readPgmPixels("color-420.y.pgm", width, height);
runner.expectTrue(decoded.result == ProgressiveJpeg::Result::Ok, "valid JPEG decodes");
runner.expectEq(width, int(decoded.info.width), "reference width");
runner.expectEq(height, int(decoded.info.height), "reference height");
runner.expectEq(expected.size(), decoded.pixels.size(), "all pixels decoded");
if (expected.size() == decoded.pixels.size()) {
  bool match = true;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (std::abs(int(expected[i]) - int(decoded.pixels[i])) > 1) match = false;
  }
  runner.expectTrue(match, "complete grayscale output matches reference");
}
```

### 2. Reproduce the input defects before correcting them

- [ ] Add byte-level mutations of the valid fixtures. Use a length-aware JPEG marker walk in the test to locate SOF2, DQT, DHT, and SOS segments; do not search for marker bytes inside compressed payload. Add these failing cases before changing the corresponding decoder code:

| Case | Construction | Required result |
|---|---|---|
| Repeated frame | Change first SOF2 luma sampling from 2 × 2 to 1 × 1; insert its original 2 × 2 frame before the first scan | `InvalidData`, no output, no sanitizer write |
| Missing EOI | Remove the final `FF D9` | `InvalidData` |
| Early EOF | Cut after the first complete DC scan, without EOI | `InvalidData` |
| Short entropy with EOI | Remove entropy bytes but retain the terminating marker | Failure; no invented zero bits |
| Table selector alias | Set a used SOS DC or AC selector to 4 | `InvalidData`, not an alias of table zero |
| Missing/zero quantizer | Remove a used DQT or set one entry to zero | `InvalidData` |
| Invalid scan progression | Refine before first transmission, repeat a first scan, or skip an approximation bit | `InvalidData` |
| Bad frame/scan IDs | Duplicate a frame ID or select one component twice in SOS | `InvalidData` |
| Output stop | Band callback returns false | `Stopped`, not `Ok` |

A missing decoder API can fail the initial build. The security regressions must also demonstrate behavioral failure against the imported upstream algorithm; a missing symbol alone does not reproduce the overflow or EOF bugs.

- [ ] Keep a valid non-512-aligned JPEG case. Append a legal COM segment before SOF if necessary to select the remainder. It must still match the complete reference pixels. This catches an incorrect rule that requires every read to return 512 bytes.

For example, the missing-EOI regression uses the `decodeBytes` helper defined above:

```cpp
std::string truncated = readFixture("color-420.jpg");
runner.expectTrue(truncated.size() > 2, "fixture contains JPEG data");
truncated.resize(truncated.size() - 2);
runner.expectTrue(decodeBytes(truncated).result == ProgressiveJpeg::Result::InvalidData,
                  "missing EOI cannot publish a completed image");
```

### 3. Correct file, marker, and frame handling

- [ ] Give `Source` an immutable file size, an abort callback, and a sticky failure result. Check the 512 KiB cap before narrowing file offsets. Use checked, clamped refills instead of treating failed seeks as EOF:

```cpp
if (pos >= fileSize) return -1;
if (shouldAbort && shouldAbort(abortUser)) {
  failure = Result::Aborted;
  return -1;
}
const size_t requested = std::min<size_t>(INPUT_BUFFER, fileSize - pos);
if (!file->seek(pos)) {
  failure = Result::IoError;
  return -1;
}
const int count = file->read(buffer, requested);
if (count != static_cast<int>(requested)) {
  failure = Result::IoError;
  return -1;
}
start = pos;
length = static_cast<uint16_t>(count);
```

The snippet replaces the refill body; cache hits retain the existing indexed access. Propagate `Aborted` and `IoError` before a generic `InvalidData` result. Do not clear a sticky failure on later reads.

- [ ] In both probe and indexing, reject lengths below two and lengths greater than the remaining file span. Do subtraction-based bounds checks before addition. Require SOI once. Probe may return after the first validated frame; full indexing must reach EOI before decode can succeed. Reject SOS before a frame and a second SOF before modifying the stored geometry:

```cpp
if (st.frameSeen) return Result::InvalidData;
```

- [ ] Validate frame precision, dimensions, component count, unique component IDs, quantizer selectors, and sampling factors. Compare all indexed fields with the caller's `ImageInfo`, including per-component sampling and IDs. Reject any difference before `decodeBand`.
- [ ] Interpret APP14 Adobe transform metadata: transform 1 is YCbCr for three components; transform 0 with three components is RGB and is unsupported; transform 2 is unsupported. Reject explicit `R`, `G`, `B` component IDs and four-component frames. Without a contrary indicator, use conventional YCbCr. Check metadata found after SOF during indexing as well as metadata before SOF.

### 4. Correct tables, scan order, entropy, and cancellation

- [ ] Track quantizer presence and reject zero entries. Do not overwrite a quantizer already used by indexed scans with a different definition. Validate DHT lengths, code-space bounds, symbol counts, and selectors without masking invalid values. Preserve the bounded table pool and scan-specific table references.
- [ ] Add a fixed `int8_t approximation[3][64]`, initialized to -1. Validate all scan headers, including chroma-only headers, before deciding whether to retain a luma scan. Require DC range 0–0; require AC range 1–63 with one selected component; require `Ah == 0` for first transmission or `Ah == Al + 1` for refinement. Reject duplicate selected components. For each covered coefficient:

```cpp
int8_t& previous = st.approximation[component][coefficient];
if (scan.ah == 0) {
  if (previous != -1) return Result::InvalidData;
} else if (previous != static_cast<int8_t>(scan.ah)) {
  return Result::InvalidData;
}
previous = static_cast<int8_t>(scan.al);
```

Validate the entire header before committing this state. Require luma DC transmission before AC. A legal DC-only image with EOI is distinct from a truncated image without EOI; do not require every coefficient to have an AC scan merely to detect EOF.

- [ ] Stop `BitReader::fill` at a marker without adding fabricated bits. Track the actual available bit count. `bits(n)` fails when fewer than `n` real bits remain. The fast Huffman lookup may consume a result only when its code length is available; otherwise use the canonical lookup with the available bits. A valid final short Huffman code must not require 16 physical bits of lookahead.
- [ ] Reject AC runs beyond `scan.se`, invalid first-scan AC sizes, invalid refinement symbols, and EOB runs beyond their scan/restart boundary. Keep coefficient indexes within 0–63. Use a wide intermediate for DC predictor scaling before clamping; do not shift a negative signed value or overflow `int32_t`.
- [ ] Check expected RST0–RST7 order, reset predictors and EOB state at restart boundaries, and check abort while consuming marker fill bytes. Do not scan unchecked through arbitrary bytes to hide a malformed restart.
- [ ] Check abort before indexing, on every refill, between entropy blocks, and during IDCT output blocks. A buffer of stuffed bytes or repeated `FF` bytes must not bypass polling. Test immediate abort and abort after work has begun; the callback counts are test observations, not production telemetry.
- [ ] Reject one-byte-short workspace before output. Check the eight-byte alignment slack using subtraction before placement construction. On every return, destroy state and check file-position restoration.

### 5. Run the decoder checks

- [ ] Run the focused target after each completed correction group:

```bash
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/build --target ProgressiveJpegTest
./test/build/bin/ProgressiveJpegTest
```

Expected: pixel fixtures pass; malformed files fail safely; valid partial final buffers pass; cancellation and callback stop remain distinct from success.

- [ ] Add committed, reproducible synthetic fixtures for 20 × 20, 2048 × 3072, and the 32/33-luma-scan boundary. Generate them once with `cjpeg`; normal tests must not require that tool. Use constant-gray maximum-size images so every expected pixel is known without a multi-megabyte reference file. Create a legal scan script with one DC scan, split first AC transmissions into disjoint ranges, and adjust the number of ranges to reach each boundary. Do not use repeated invalid DC scans as the sole scan-limit test.

Keep the generator at `test/fixtures/progressive-jpeg/generate.py`. This is the concrete generation pattern; add the color maximum-size fixture with a P6 source of repeated `(128, 128, 128)` pixels:

```python
from pathlib import Path
import subprocess
import tempfile

destination = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as directory:
    temporary = Path(directory)
    for width, height in ((20, 20), (2048, 3072)):
        source = temporary / "gray.pgm"
        source.write_bytes(f"P5\n{width} {height}\n255\n".encode()
                           + bytes([128]) * width * height)
        subprocess.run(
            ["cjpeg", "-quality", "80", "-progressive", "-outfile",
             str(destination / f"gray-{width}x{height}.jpg"), str(source)],
            check=True)
    source = temporary / "scans.pgm"
    source.write_bytes(b"P5\n20 20\n255\n" + bytes([128]) * 400)
    for count in (32, 33):
        ac_count = count - 1
        entries = ["0: 0 0 0 0;"]
        entries += [f"0: {k} {k} 0 0;" for k in range(1, ac_count)]
        entries += [f"0: {ac_count} 63 0 0;"]
        scan_file = temporary / "scans.txt"
        scan_file.write_text("\n".join(entries) + "\n")
        subprocess.run(
            ["cjpeg", "-quality", "80", "-scans", str(scan_file), "-outfile",
             str(destination / f"gray-{count}-scans.jpg"), str(source)],
            check=True)
```

Document the generator tool version with the fixtures. Confirm actual SOS counts before using these files as the boundary regression.
- [ ] Exercise exact 512 KiB and 512 KiB + 1 file lengths using legal COM padding before the frame. The exact-limit input retains the original pixels; the oversized input fails before allocation.
- [ ] Run the sanitizer build in a temporary build directory:

```bash
cmake -S test -B /tmp/papyrix-progressive-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build /tmp/papyrix-progressive-asan --target ProgressiveJpegTest
./test/build/bin/ProgressiveJpegTest
```

The current test CMake places executables in `test/build/bin` even when its build directory is elsewhere. Do not run sanitizer and ordinary builds concurrently. Restore the ordinary executable with `cmake --build test/build --target ProgressiveJpegTest --clean-first`; a plain incremental build can retain the newer sanitizer executable. Remove the temporary build directory after verification.

**Review gate:** complete-pixel output, safe malformed-input handling, bounded state, and no hidden allocation. Run `make test`, `make format`, and `make check` before a commit; inspect formatter changes. Commit only Task 1 files with subject `Add checked progressive JPEG decoding for S3`.

## Task 2: Separate PSRAM-backed converter

**Consumes:** Task 1 decoder API, `ImageConvertConfig`, `Print`, `GrayscaleRowScaler`, and BMP helpers.

**Produces:** `ProgressiveJpegImageConverter` and the `ProgressiveJpegConverterTest` target.

### 1. Add converter behavior tests first

- [ ] Link the real decoder, converter, and `BitmapHelpers.cpp` into the new test target. Use the existing host heap and SD mocks for fault injection, not decoder or scaler mocks. Compile with `PAPYRIX_TARGET_X4PRO=1`.
- [ ] Test direct converter calls with both `oneBit` values, no scaling, and smaller output bounds. Parse BMP dimensions, bit depth, palette, stride, and every packed pixel. First validate decoder luma against independent references with the one-level tolerance from Task 1. Then compare converter output against the existing scaler/dither pipeline fed those validated luma rows. Do not require bit-identical dither output from grayscale references that differ by one level; error diffusion can amplify that difference. Also use constant black/white fixtures with independently known packed pixels to catch shared-pipeline mistakes.
- [ ] Use the existing PSRAM controls for absence and allocation refusal:

```cpp
testSetHeapPool(MALLOC_CAP_INTERNAL, 400000, 400000);
testSetHeapPool(MALLOC_CAP_SPIRAM, 0, 0);
ProgressiveJpegImageConverter converter;
runner.expectFalse(converter.convert(input, output, config),
                   "missing PSRAM refuses progressive conversion");
testResetLargestFreeBlock();
testSetPsramAllocationFailure(true);
runner.expectFalse(converter.convert(input, output, config),
                   "PSRAM allocation failure has no internal fallback");
testResetLargestFreeBlock();
```

Open fresh input/output handles for each scenario. `input`, `output`, and `config` use the existing `FsFile`, `Print`, and `ImageConvertConfig` interfaces. Check unchanged output bytes on admission failure and released workspace on subsequent failures. Restore mock state between cases.

### 2. Allocate and stream without copying image bands

- [ ] Add the two converter files and the decoder dependency. Guard the entire implementation with `PAPYRIX_CAP_PROGRESSIVE_JPEG`. Keep all per-call resources in local RAII objects.
- [ ] Probe once, calculate `workspaceBytes(info)`, and calculate an upper bound for other buffers before constructing allocation-owning helpers. The source width bounds the output width, so it can bound scaler and dither memory without duplicating the scaler's float sizing calculation. Include nine bytes per possible output column for scaler arrays, six bytes per dither column plus its padding, the dither object, and the packed row.

```cpp
constexpr size_t reserve = 20 * 1024;
constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
const size_t psramLargest = heap_caps_get_largest_free_block(caps);
if (workspaceSize == 0 || workspaceSize > psramLargest ||
    totalRequired > largest || reserve > largest - totalRequired) {
  return false;
}
std::unique_ptr<uint8_t, decltype(&heap_caps_free)> workspace(
    static_cast<uint8_t*>(heap_caps_malloc(workspaceSize, caps)), &heap_caps_free);
if (!workspace) return false;
```

`workspaceSize` is Task 1's reported size. `totalRequired` includes that size and the buffer upper bound. Check the free-size admission as well if the largest-block calculation does not cover a selected pool. Never substitute `malloc` after a PSRAM allocation failure.

The current heap mock already has separate internal and PSRAM pools. Reuse `testSetHeapPool` and `testSetPsramAllocationFailure`. For failed-read and failed-seek cases, reuse the existing `SdMan` controls where possible. If the mock cannot fail a normal seek or an individual short refill, add only that fault-injection control in `test/mocks/SdFat.h` and reset it between tests. Do not change successful file behavior to make a regression pass.

- [ ] Construct and validate the shared scaler, packed row, and selected ditherer. Preserve the existing optional quantization fallback; if `requireDithering` applies and allocation fails, return false. The approved scaler correction uses 32-bit pixel counts.
- [ ] Use a local band context containing references to the output, config, scaler, and row buffer, plus source/output row counters. Define its callback as:

```cpp
static bool consumeBand(void* user, uint16_t y, const uint8_t* gray,
                        uint16_t width, uint16_t rows, uint16_t stride);
```

Check source row order, width, stride, and remaining height before accessing the buffer. Pass `gray + row * stride` directly to the scaler. Create the scaler's emission callback once per conversion, not on each pixel or band.

- [ ] Account for the scaler's existing floor-based final boundary: it can finish all output rows before consuming the final source row. Once output rows reach `outHeight`, continue decoder validation and source-row accounting, but do not call `accumulate` again. Do not stop the decoder early and miss later entropy errors.
- [ ] Clear packed row padding. For 1-bit Atkinson output pass the unadjusted grayscale value; that ditherer applies adjustment internally. For 2-bit output apply `adjustPixel` once before Atkinson. Use the emitted output-row index for fallback quantization. Advance the ditherer once per emitted row.
- [ ] Check header and row writes. Return true only for `Result::Ok`, exactly `info.height` source rows, and exactly the scaler's output height. `Stopped`, `Aborted`, and every error return false. Check cancellation before allocations and between emitted rows.

### 3. Verify resource release and pixel output

- [ ] Run:

```bash
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/build --target ProgressiveJpegConverterTest
./test/build/bin/ProgressiveJpegConverterTest
```

Expected: complete 1-bit/2-bit output, including odd-size downscaling; no internal fallback; checked write failure; no success on early callback stop. Repeat valid conversion after each fault to check state and resource cleanup. Include output widths that need BMP row padding.

**Review gate:** no full-image buffer, no repeated band copy, complete memory accounting, and exact row handling. Run the required precommit checks and commit Task 2 files with subject `Add a separate progressive JPEG image converter`.

## Task 3: Factory routing, publication, and end-to-end proof

**Consumes:** Tasks 1 and 2, the existing factory, file publication path, and target-symbol checker.

**Produces:** the approved feature on both S3 targets, unchanged C3 decoding, production documentation, and verification evidence.

### 1. Add production factory regression coverage

- [ ] Add `ProgressiveJpegFactoryTest.cpp`. Build it once as `ProgressiveJpegFactoryX4PROTest`, once as `ProgressiveJpegFactoryX4CLASSICTest`, and once as `ProgressiveJpegFactoryXTEINK_C3Test`, with exactly one target macro per executable. Follow the existing early `foreach`/`continue` multi-target pattern in `test/CMakeLists.txt:69-107`.
- [ ] Link production factory, progressive converter, decoder, baseline JPEG converter, picojpeg, PNG converter, pngle, Bitmap, BitmapHelpers, PackedCoverReducer, and FsHelpers. Keep the real converter/decoder include directories before converter test mocks. Resolve pngle from the installed dependency or its existing local source; do not silently replace it with a fake decoder.
- [ ] In existing factory-linked host targets, add explicit C3 target selection: `ContentHandleThumbnailTest`, `EpubIndexAnchorsTest`, `EpubTocRecoveryTest`, `PngOneBitTest`, `JpegThumbnailCancellationTest`, `BmpConversionTest`, `ImageConverterSignatureTest`, and `ImageFileViewTest`. Do not set a global C3 definition that conflicts with S3 tests.
- [ ] Keep useful signature, stack, and validation-failure checks in `ImageConverterSignatureTest`. Delete assertions that merely compare output to mock strings such as `"JPEG"` or `"PNG"`; do not change those strings to new expected wiring. Cover actual routing with decoded pixels in the new target.
- [ ] Register real progressive bytes under a misleading `.png` filename. S3 conversion succeeds with the expected BMP pixels; C3 refuses it. Baseline JPEG succeeds on all targets. PNG and BMP output remain correct.
- [ ] Add a sync-failure regression before the publication change:

```cpp
SdMan.registerFile("/output.bmp", "previous-output");
SdMan.setSyncResult(false);
runner.expectFalse(ImageConverterFactory::convertToBmp(
    "/progressive.jpg", "/output.bmp", config), "failed sync refuses publication");
runner.expectFalse(SdMan.exists("/output.bmp.part"), "failed sync removes temporary output");
runner.expectEq(std::string("previous-output"),
                SdMan.open("/output.bmp", O_RDONLY).getBuffer(),
                "failed sync preserves existing output");
```

Also cover failed writes, failed seek/read, malformed progressive input, output-validator rejection, and cancellation. Do not assert preservation after the replacement operation itself has begun; that is outside the approved storage contract.

### 2. Select the separate converter and check synchronization

- [ ] Keep public signature detection unchanged. Add an S3-only private factory helper with this signature:

```cpp
ImageConverter* selectJpegConverter(FsFile& input,
                                    const std::function<bool()>& shouldAbort);
```

It returns the existing baseline instance for SOF0/SOF1, the new instance for SOF2, or null on invalid/unsupported inspection. Save and restore the entry position; failed restoration returns null. Check SOI, marker fill bytes, two-byte segment lengths, file bounds, failed reads/seeks, and abort. Stop at SOF; do not scan entropy data. Do not impose the progressive-only size cap on baseline JPEGs. Arithmetic markers do not select the progressive converter.

- [ ] In `convertToBmp`, use that helper only when signature detection reports JPEG and the target capability is enabled. Leave C3 on the original selection path. Never retry baseline conversion after SOF2 conversion fails.
- [ ] Extend the existing publication condition before closing the output file:

```cpp
const bool converted = converter->convert(inputFile, outputFile, config);
const bool success = converted && outputFile.sync();
inputFile.close();
outputFile.close();
```

The existing failure cleanup, configured validator, and `commitFile` remain responsible for the next steps. Keep a single publication path. This checks durability errors for the shared factory without changing baseline pixel processing.
- [ ] Do not edit failure-marker paths, cache versions, or EPUB retry policy. Do not add a new progressive entry to the public `ImageFormat` enum or its desktop mock.

### 3. Prove target exclusion

- [ ] Extend the existing artifact checker, rather than add another checker:

```python
GROUPS["progressive JPEG symbols"] = re.compile(
    r"ProgressiveJpeg::|ProgressiveJpegImageConverter"
)
```

Add that group only to `FORBIDDEN_GROUP_NAMES["default"]`. Extend the existing checker tests with a real demangled decoder function name and converter type name. Verify rejection on C3 and acceptance on each S3 target. The checker does not replace testing actual compiled artifacts.

- [ ] Run focused factory and checker tests, then the full host suite before firmware builds:

```bash
cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/build --target ProgressiveJpegFactoryX4PROTest ProgressiveJpegFactoryX4CLASSICTest ProgressiveJpegFactoryXTEINK_C3Test
./test/build/bin/ProgressiveJpegFactoryX4PROTest
./test/build/bin/ProgressiveJpegFactoryX4CLASSICTest
./test/build/bin/ProgressiveJpegFactoryXTEINK_C3Test
python3 test/scripts/test_target_features.py
make test
make format
make check
pio run -e release_xteink_c3 -e release_x4pro -e release_x4c
```

Inspect formatter changes. Run the existing symbol checker on demangled `nm -C` output from each resulting ELF, with target names `default`, `x4pro`, and `x4c`. Use the matching toolchain for C3 and S3. Also inspect the C3 map for retained progressive objects or data; an unused source file must not contribute decoder state to firmware.

### 4. Run a real-file smoke conversion

- [ ] Create a temporary host driver with the following main function. Compile the production factory and converters, not the desktop reader's `ImageConverter` mock:

```cpp
#include <ImageConverter.h>
#include <cstdlib>

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  ImageConvertConfig config;
  config.maxWidth = 480;
  config.maxHeight = 800;
  config.oneBit = std::strtol(argv[3], nullptr, 10) == 1;
  config.requireDithering = true;
  config.shouldAbort = [abort = std::strtol(argv[4], nullptr, 10) != 0]() {
    return abort;
  };
  return ImageConverterFactory::convertToBmp(argv[1], argv[2], config) ? 0 : 1;
}
```

- [ ] Use the real-file `FsFile` and `SDCardManager` adapters from `tools/reader-test/mocks/`. Put production image-converter headers first in the include path. Link the same production source set as the factory test. Supply temporary heap-capability and task-stack shims for the host; return a documented simulated PSRAM pool and at least 4096 stack bytes. Do not report those simulated values as device measurements. The existing reader tool heap/task shims lack the complete new allocation interface, so do not assume its default build proves this path.
- [ ] Run the temporary executable as `image-smoke INPUT OUTPUT 1 0` and `image-smoke INPUT OUTPUT 2 0`. Decode the BMPs independently and compare dimensions and every visible pixel with the converter reference checks. Observe an actual rendered BMP preview. Then run with final argument `1`; require failure and no `.part` file. Repeat with a malformed JPEG and with an existing completed output. Remove the temporary driver and build files afterward.
- [ ] Use a generated 2048 × 3072 fixture within the compressed-size limit for the smoke run. Exercise odd dimensions and a valid final partial input buffer. Do not replace the full path with a test of decoder callbacks alone.

### 5. Update current-state documentation and complete the device checks

- [ ] After the host smoke succeeds, update `docs/images.md` in place. Replace the blanket progressive-JPEG rejection with target-specific support. Keep arithmetic coding unsupported. State the new progressive limits, PSRAM requirement, and unchanged old failure markers. Keep baseline conversion instructions useful for C3 owners. Use this current-state wording:

> X4 Pro and X4 v2 Classic support 8-bit progressive JPEG images within the progressive conversion limits. X3 and original X4 support baseline JPEG only. Progressive conversion requires available PSRAM. Previously failed images remain skipped until their cache is removed.

Do not put benchmark logs, patch history, design choices, or unfinished behavior in `docs/`. Update the approved spec status only when implementation and reported checks justify it.

- [ ] On each S3 device, use the same book and standalone-image fixtures. Exercise a progressive inline image, cover, thumbnail, and standalone image. Check display output, repeated conversion, cancellation, memory release, largest free block, and stack use. Confirm the target before flashing; do not flash or erase a device without authorization.
- [ ] Record unavailable hardware checks as unverified. Do not infer them from host timings, heap shims, or the nominal PSRAM size. Do not mark device acceptance complete without device evidence.
- [ ] Commit only the factory integration, regression checks, target checker changes, and current-state documentation after the required checks. Suggested subject: `Enable progressive JPEG conversion on S3 readers`.

Verification: all 226 host suites pass. The decoder passes 59 checks under AddressSanitizer and UndefinedBehaviorSanitizer. Cancellation checks stop after the first decoded band and inside stuffed-byte and marker-fill sequences. Fourteen real-file conversions pass. All three release firmware builds and target-symbol checks pass. C3 decoder objects contain no code or data. Formatting passes. Static analysis passes with one medium and 40 low findings in unchanged code. Compiler warnings remain in unchanged files.

Classic firmware is flashed through `release_x4c`; the uploader verifies its hash. The chip reports 8 MB embedded PSRAM. The independent test book and three images are uploaded to `/jpeg-test-931d62ez`; downloaded sizes and SHA-256 hashes match. The user confirms that EPUB images 1–6 and 8 display. Image 7 is deliberately truncated. The serial log confirms its rejection and successful conversion of image 8 afterward. It also confirms progressive cover conversion and thumbnail publication. The two maximum-image chapters complete cache generation in 5950 ms and 5459 ms; these are not isolated decoder timings.

The user also confirms correct standalone display of `color-420.jpg` and `gray-maximum.jpg`. The serial log confirms that both files convert to BMP and enter the standalone image cache.

The user skips the remaining Classic checks: cover and thumbnail appearance, forced repeated conversion, memory release, free PSRAM, largest free block, stack use, isolated conversion time, and hardware cancellation response. These checks are unverified, not passed.

Pro firmware is flashed through `release_x4pro`; the uploader verifies its hash. The chip reports 8 MB embedded PSRAM. The same independent fixtures are uploaded to `/jpeg-test-yxk_j3cd`; downloaded sizes and SHA-256 hashes match. The user confirms that EPUB images 1–6 and 8 display. The serial log confirms rejection of the deliberately truncated image 7 and successful conversion of image 8 afterward. It also confirms progressive cover conversion and thumbnail publication. The two maximum-image chapters complete cache generation in 6403 ms and 5764 ms; these are not isolated decoder timings.

The user confirms correct standalone display of `color-420.jpg` and `gray-maximum.jpg` and confirms that Back works. The serial log confirms conversion and caching of both progressive files and the baseline JPEG.

A temporary Pro boot-time diagnostic runs the production factory ten times on the 2048 by 3072 grayscale fixture. Eight conversions complete with alternating 1-bit and 2-bit output and required dithering. Two conversions stop after a separate timer requests cancellation one second after conversion starts. Both requests occur after 50 decoded bands. The factory returns after 8600 and 9531 microseconds, including temporary-file cleanup. Cancellation preserves the existing output hash and size in one case and leaves no completed output in the other. Later conversions succeed. Heap integrity checks pass after all ten runs.

The diagnostic reports 8388608 bytes of PSRAM and a 63320-byte decoder workspace. Free PSRAM returns to 8384784 bytes after every run. Its largest free block remains 8257524 bytes. Free internal RAM returns to 206900 bytes, with a 147444-byte largest block. Samples taken every 10 ms record a minimum of 8321292 free PSRAM bytes and 199192 free internal RAM bytes. The lowest recorded free stack is 11572 bytes on `loopTask`; this is the task lifetime high-water mark, not isolated decoder stack use. Successful factory calls take 7.434436 to 7.574981 seconds with instrumentation. These boot-time measurements do not represent normal reader-task timing or stack depth. One serial memory line is truncated; its on-device memory equality check passes.

The temporary diagnostic code is removed. Both source files match their pre-diagnostic bytes. All 226 host suites pass again. The Pro release is restored, its SHA-256 matches the original verified release, and normal reader startup succeeds. The user confirms correct appearance of the Pro test book cover and Home thumbnail. The user skips the remaining Classic checks.

Normal Pro reading uses a fresh independent book at `/jpeg-reader-x57qs0pa/reader-load.epub`. Temporary converter logging records nine calls through the actual `loopTask` and `PageCache` paths. Seven conversions succeed. A reader stop request cancels the background maximum-size image after 36 decoded bands; the foreground path then converts the same image successfully. The deliberately damaged image fails, and the following valid image succeeds. The user completes the workload and returns Home.

The normal-reader samples record at least 8114076 free PSRAM bytes, a 7995380-byte largest PSRAM block, 156612 free internal RAM bytes, and a 114676-byte largest internal block. The task lifetime free-stack minimum is 11564 bytes on `loopTask` and 11864 bytes on `PageCache`. The converter samples after buffer allocation and at decoded-band boundaries, with at least 10 ms between periodic samples. Eight calls have complete before-and-after heap records; all four heap values return to their starting values. The ninth record is truncated, and its available fields match. Other missing serial fields remain unverified. The maximum-size foreground converter calls take 5.722052 to 6.510040 seconds with instrumentation, excluding factory synchronization and publication.

The normal-reader logging is removed, and the converter matches its pre-test bytes. All 226 host suites pass again. The release image matches SHA-256 `828b8a9c82307d5af05c20cd10842b078723200c877f8a6ef3c71067d56e4e9a`. The uploader verifies its writes, and normal reader startup succeeds after restoration. Serial capture is stopped.

## Completion checklist

- [x] Task 1 decoder passes pixel, malformed-input, limit, and cancellation checks under sanitizers.
- [x] Task 2 converter passes complete-pixel and allocation/output failure checks.
- [x] Task 3 production factory passes on Pro, Classic, and C3 host configurations.
- [x] Real-file smoke conversions produce valid 1-bit and 2-bit BMPs; error paths leave no completed partial image.
- [x] C3 firmware contains no progressive decoder code or state; both S3 firmware builds succeed.
- [x] Existing cache markers and baseline routing remain unchanged. Baseline output rejects short writes and retains correct pixel values during large reductions.
- [x] Required repository checks run; failures and existing warnings are reported accurately.
- [x] Device evidence is supplied for both S3 targets, or each unavailable check is clearly listed as unverified.
- [x] `docs/images.md` describes only implemented behavior. No throwaway harness or development log remains there.

## Execution handoff

Recommended: inline execution with `executing-plans`. The three tasks depend on one another and share test build configuration. Stop for a review at each task boundary, not for approval of every mechanical edit.

Alternative: subagent-driven execution with fixed interfaces and one integration owner. Delegate substantial independent code slices only; do not assign tiny edits or concurrent ownership of `test/CMakeLists.txt`. Verification runs after the relevant edits settle.
