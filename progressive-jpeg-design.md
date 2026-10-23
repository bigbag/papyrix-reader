# S3 Progressive JPEG Design

Date: 2026-10-22

Status: Implementation, host tests, sanitizer checks, release builds, and target-symbol checks pass. Classic and Pro display the valid EPUB and standalone fixtures and reject the deliberately damaged EPUB image. Pro cover and Home thumbnail appearance pass user verification. Pro boot-time and normal-reader measurements cover memory release, stack headroom, and cancellation. Temporary instrumentation is removed and release firmware is restored. The user skips further Classic checks.

## Goal and scope

Add progressive JPEG conversion on X4 Pro and X4 v2 Classic. Both targets use ESP32-S3. Keep X3 and original X4 on the existing baseline JPEG converter. Exclude the new decoder and converter from C3 firmware.

Use a separate progressive converter, as selected during design review. Base the decoder on Witchhunt revision `e05065f6c3f16a12dad5bc1313046ae3f4f203f1`. Correct its input-validation and cancellation defects before use. Preserve the MIT license and attribution notices.

Apply support through the shared image conversion factory. Existing callers for book images, covers, thumbnails, and standalone images use that route without a new setting.

Do not change existing image failure markers or cache versions. Previously failed images remain skipped until the user removes their cache. This feature applies to new conversions only.

Keep this specification outside `docs/`. Production documentation describes implemented behavior, supported targets, and limits only.

Approved scope extension: check baseline JPEG header and row writes. Use 32-bit pixel counts in the baseline and shared row scalers to prevent overflow during large reductions. Update their memory estimates. These corrections apply to all targets.

## Selected approach

The alternatives were a branch inside the baseline JPEG converter, a separate progressive converter, and decoder-side reduced-size output. The selected approach uses a separate converter and full-size decoding.

This separates progressive decoding from picojpeg. Reuse existing scaling, dithering, and BMP helpers rather than copy the baseline converter. Do not add a new converter registry or change the public image-format enum. Do not add resumable decoding, checkpoint files, telemetry, or a project language-version change.

## Components

### Target capability

Add a progressive-JPEG capability to `lib/BoardSupport/include/TargetConfig.h`. Enable it for Pro and Classic. Disable it for C3. Guard the new converter, decoder, and factory selection code with this capability. Library dependencies must not introduce progressive decoder code or state into the C3 artifact.

### Decoder

Add a local `lib/ProgressiveJpeg/` library using the existing library layout. Adapt file access to `FsFile` and retain C++17 compatibility. The decoder receives caller-owned workspace and an abort callback. It emits full-size grayscale rows in ordered bands. It does not allocate a whole-image pixel buffer or own output files.

Keep only the API needed to inspect an image, calculate workspace, and decode it. Do not import the checkpoint and resume API. The decoding state belongs to each conversion, not a mutable global.

### Progressive converter

Add `ProgressiveJpegImageConverter` as a separate implementation of the existing `ImageConverter` interface. Keep its implementation in a separate source file in `lib/ImageConverter/src/`.

The converter owns workspace and output-processing buffers for one call. Reuse `GrayscaleRowScaler`, `Atkinson1BitDitherer`, `AtkinsonDitherer`, `write1BitBmpHeader`, and `write2BitBmpHeader`. Follow existing pixel-adjustment and quantization conventions. Apply adjustment once per pixel. Respect `oneBit`, `requireDithering`, maximum output dimensions, and `shouldAbort`.

### Factory

On S3, inspect JPEG marker segments to select the progressive converter for SOF2. Check segment lengths and file bounds during inspection. Restore the input position before conversion. Invalid inspection or failed position restoration fails conversion.

Public signature detection continues to report `ImageFormat::Jpeg`. Baseline JPEG continues through `JpegImageConverter` and picojpeg. PNG and BMP selection remain unchanged. Never try the baseline decoder after a progressive conversion fails.

The factory owns input and output files, temporary-file cleanup, optional output validation, and publication. Do not duplicate these responsibilities inside the new converter.

## Input contract

The new progressive path accepts:

- Huffman-coded SOF2 with 8-bit samples.
- One-component grayscale or three-component YCbCr. The first component carries luma. Each sampling factor is from 1 through 4. Luma uses the maximum horizontal and vertical factors present in the frame.
- Source dimensions from 20 by 20 through 2048 by 3072 pixels, inclusive.
- Compressed input no larger than 512 KiB.
- At most 32 luma scans and 16 retained Huffman table definitions.

Reject arithmetic coding, unsupported precision, unsupported color spaces, and unsupported sampling. Three-component files without a contrary color-space indication use conventional YCbCr interpretation. Do not treat explicit RGB, CMYK, or YCCK data as luma. These are limits for the new progressive path, not changes to baseline conversion.

Reject repeated frame headers. Keep dimensions, component definitions, and sampling fixed after workspace calculation. Validate component IDs, table selectors, required tables, segment bounds, and progressive scan order before use. Do not mask an invalid selector into a valid table slot. Validate coefficient indexes and refinement transitions before memory access.

Require an end-of-image marker and successful decoding of all required output rows. Premature EOF is not success. A decoder error after some rows have been emitted still fails the entire conversion.

For each buffered read, request `min(bufferCapacity, fileSize - offset)` bytes after checking that the offset is within the file. A final buffer shorter than 512 bytes is valid. A read shorter than the clamped request is a failure. Failed seeks and failed restoration of file position are failures.

## Memory and cancellation

Retain the factory's 4096-byte stack admission check. Calculate the decoder workspace and conversion buffer requirements before allocation. Include scaler, dither, and packed output-row storage. Keep the existing 20 KiB allocation reserve.

Check `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` against the complete requirement plus reserve. Allocate decoder workspace from memory with both `MALLOC_CAP_SPIRAM` and `MALLOC_CAP_8BIT`. Check the largest block with those capabilities as well. Check every allocation result. Release all conversion-owned memory on every exit.

If PSRAM is absent or insufficient, fail conversion. Do not fall back to a large internal-RAM workspace. The target model and build flags alone do not prove that usable PSRAM exists.

Check cancellation before work, on input-buffer refills, and between decode blocks. Apply the checks during indexing, stuffed-byte handling, marker handling, and restart recovery. Output processing also checks cancellation between rows. Cancellation returns failure and releases resources; it does not create a completed BMP.

## Output contract

Decode full-size bands, scale their rows to the configured output bounds, then dither and pack the output. Preserve aspect ratio and do not enlarge images that already fit. Require ordered source rows and the expected output-row count.

Check BMP header writes and every packed-row write. Treat a callback stop caused by an output error as conversion failure, not successful partial output. If required dithering cannot be allocated, fail rather than silently omit it. Otherwise retain the existing quantization fallback policy.

Write only to the factory's `.part` path. Before publication, require conversion success, the expected row count, successful file synchronization, and successful configured output validation. Close files before publication. On failure, remove the temporary output. Existing completed output remains untouched before the publication step.

Use the existing `commitFile` operation. Do not claim that replacement is power-loss atomic. Do not broaden this feature into a storage-replacement redesign.

## Verification

### Decoder regression checks

Generate new, deterministic image fixtures locally. Do not use Witchhunt images. Encode with `cjpeg` and generate independent grayscale references with `djpeg`. Compare complete grayscale output against those references. Cover grayscale, YCbCr, restart markers, odd dimensions, maximum dimensions, and source sizes not divisible by 512. Use a maximum difference of one grayscale level as the pixel acceptance threshold.

Keep regression cases for repeated SOF2 with changed sampling, missing end-of-image markers, truncated headers and entropy data, invalid table references, invalid scan transitions, and scan-count boundaries. Run malformed-input cases with AddressSanitizer and UndefinedBehaviorSanitizer. Valid final partial buffers must still decode.

Check insufficient workspace, absent PSRAM, allocation failures, and cancellation during indexing and entropy decoding. Include stuffed-byte and marker sequences that previously bypassed abort polling.

### Converter and factory checks

Exercise the production decoder, converter, scaler, and factory with real input and output files. Compare dimensions and complete output pixels for scaled 1-bit and 2-bit BMPs. Check failed writes, failed synchronization, output-validation rejection, callback failure, cancellation, and temporary-file cleanup. Check that errors before publication preserve an existing output.

Check baseline JPEG, PNG, and BMP behavior through the existing factory. Keep signature detection independent of the filename extension. Do not rely only on the desktop reader: its `ImageConverter` mock does not perform image conversion.

Do not add automatic retries or cache migration. Existing saved failures must continue to suppress conversion until cache removal.

### Build and device checks

Run host tests before firmware builds. Run the repository formatting and static-analysis checks. Build Pro, Classic, and C3 firmware. Inspect the C3 artifact for absence of progressive decoder code and state.

On both S3 devices, exercise progressive book images, covers, thumbnails, and standalone images. Measure usable PSRAM, the largest free block, stack use, conversion time, and cancellation response. Check displayed output and repeat conversions for retained memory. Hardware results remain unverified until these checks run; host results do not establish device behavior.

## Completion and review gate

Implementation is complete only when the agreed conversion behavior and executable checks pass. Report unavailable device checks separately. Update production documentation only after the behavior exists. Leave old failure markers and baseline JPEG behavior unchanged.

Review this written specification before creating the implementation plan. Specification approval does not waive the separate device-verification requirement.
