# S3 Dual-Core Reader Speed Design

## Intent

PapyriX S3 targets have two CPU cores and 8 MB PSRAM.
The reader still does most work in sequence.
This design uses the second core for safe background production.
It does not promise speed from the chip name alone.

The work order is fixed:

1. Cold open and indexing.
2. Next-page turn.
3. Chapter jump.
4. Layout change.

## Goals

- Make first page reading start fast.
- Move indexing ahead while the user reads.
- Reduce next-page wait after the current page is ready.
- Reduce chapter jump wait for likely targets.
- Make layout change progressive and cancellable.
- Prove gain on X4 Pro, X4 v2 Classic, and Sticky.
- Keep ESP32-C3 behavior unchanged.

## Non-goals

- Do not run two renders into one framebuffer.
- Do not parallelize e-ink refresh.
- Do not remove the SD page cache.
- Do not add raw TTF or OTF rendering.
- Do not build a full DOM cache.
- Do not use the vendor CPU claim as a speed target.

## Current facts

- S3 already uses PSRAM for the framebuffer, glyph slab, image rows, and parser scratch.
- `ReaderState` already runs a background `PageCache` task.
- The background task owns `pageCache_` and `parser_` while it runs.
- Navigation stops that task before the main thread uses those objects.
- `BackgroundTask` uses plain `xTaskCreate`.
- Plain `xTaskCreate` does not prove core separation.
- ESP-IDF can pin a task after first float use.
- The EPUB parser uses float values for CSS and line height.
- The S3 Arduino loop runs on core 1.
- The renderer, font cache, SD handles, framebuffer, and display path need one owner.

## Approaches

### A. Pinned producer only

Core 1 owns input, final render, status bar, and display.
Core 0 runs one pinned producer task.
The producer extracts EPUB data, parses XML, lays out page records, prepares images, and extends SD cache files.
The main thread stops or pauses the producer before it uses shared reader objects.

This approach helps cold open, indexing, chapter jump, and layout change.
It helps next-page turn when the wait is parse, layout, image, or cache work.
It does not cut render or refresh time.

This is the chosen first step.

### B. Producer plus off-screen pre-render

This approach adds PSRAM page bitmaps after approach A.
It needs a second render target.
It needs font cache isolation.
It needs strict invalidation for book, section, page, viewport, font, theme, and image state.

Use this step only if measurements show render time is a large part of next-page delay.

### C. Split engine

This approach splits parser, layout, font, and render into queued services.
It has the highest ceiling.
It also rewrites hot paths and raises power, memory, and refresh risk.

Do not use this approach now.

## Measurement fixture

Create one worst-case torture EPUB with images.
The file must stay deterministic.
The generator must emit the same bytes for the same inputs.

The book contains these zones:

- Many small chapters for chapter jump cost.
- One long prose chapter for indexing and next-page cost.
- Heavy CSS for open and parse cost.
- Internal links for jump targets.
- Image pages for decode and conversion cost.
- Dense paragraphs for layout change cost.
- Footnotes or endnotes for anchor cost.
- Wide tables within current parser limits.
- Text markers that name each chapter and zone.

Keep two repo sample EPUB files as sanity checks.
Do not tune the design only to the torture book.

Record these values before and after each phase:

- Time to first readable page.
- Full indexing time.
- Indexed pages per second.
- Next-page time split into cache, load, render, and display parts.
- Chapter jump time for near and far targets.
- Layout change time to first usable page.
- Layout change time to complete swap.
- Peak heap use.
- Largest free PSRAM block.
- Worker core trace.
- Current draw when test gear is available.

Run the fixture on X4 Pro, X4 v2 Classic, and Sticky.
Treat Sticky display results as unverified until hardware checks pass.

A phase passes its speed gate only when its target metric improves by at least 20 percent in the median on Pro and Classic.
No S3 target may regress by more than 5 percent in any measured reader metric.
Collect Sticky timing data in each phase.
Report Sticky results separately until its display path is hardware-verified.
Use the primary metric for each phase: first readable page time for Phase 1, next-page time for Phase 2, near and far chapter jump time for Phase 3, and time to first usable page for Phase 4.
If a phase misses the gate, stop its speed work and revisit the design before the next phase.

## Affinity design

Extend `BackgroundTask::start` with a default core argument.
Use `tskNO_AFFINITY` as the default.
Use `xTaskCreatePinnedToCore` when the caller sets a valid core.
Keep old callers source-compatible.

`ReaderState` sets the cache producer to core 0 on S3 targets.
`ReaderState` keeps the current behavior on C3 targets.
Task start logs the requested core.
Producer phase logs read the current core with `xPortGetCoreID`.
The phase must fail its speed claim if the producer runs on core 1.
Create the producer with the pinned API before any producer float work runs.
Run only the task trampoline on the producer stack.
Do not run producer work inside Wi-Fi, timer, or UI callbacks.

If pinned task creation fails, do not fall back to an unpinned producer for speed work.
Use the existing foreground path or no background speed path.
Log the failure once.

## Producer work order

### Phase 0: Pin and prove

Add affinity support.
Add core trace logs.
Change no reader behavior.
Prove that the loop stays on core 1.
Prove that the producer runs on core 0 during EPUB parse and layout.

### Phase 1: Cold open and indexing

Keep the first readable page path short.
Start the producer after the first page render completes.
Parse CSS and spine data once for the open book.
Extend page cache in current chunks.
Start with the current section.
Continue with later sections in reading order.
Yield at page and image boundaries.
Stop fast on navigation, book close, and settings change.

### Phase 2: Next-page turn

Prefetch page records for the next page in the current section.
Prefetch the first pages of the next section near a section end.
Write prefetched records only to the existing SD page cache.
Do not keep page records in a new RAM cache in approach A.
Do not add page bitmaps in this phase.
Measure load, deserialize, render, and display time separately.

### Phase 3: Chapter jump

Prefetch a small set of likely sections.
Use previous section, next section, and the last TOC target as candidates.
Cancel stale jump work on new navigation.
Keep the prefetch set small.
Do not scan the whole book after each jump.

### Phase 4: Layout change

Keep the old layout usable during re-layout.
Use a second parser instance for the new render config.
Write the new layout to a staging cache path for that render config.
Keep the old parser and old cache active on the main thread.
Publish a new section only after its staging cache header is valid.
Switch the active cache path only at a section boundary.
Cancel the job when the user changes layout again.
Cancel the job when navigation needs an old page that is not cached.
Delete or ignore stale layout work by render config fingerprint.

## Ownership rules

- The producer owns the parser while it runs.
- The producer owns the active page cache writer while it runs.
- The main thread owns the display and framebuffer.
- The main thread owns final page render.
- The producer does not call display functions.
- The producer does not draw into the framebuffer.
- The producer checks stop state at page, image, and section boundaries.
- Producer stop checks must keep p95 stop latency at or below 250 ms.
- Producer stop latency must stay at or below 1000 ms.
- Split any work chunk that breaks the stop budget.
- The main thread keeps the old page on screen if stop does not finish in time.
- The main thread stops the producer before navigation uses the parser or page cache.
- The main thread stops or pauses the producer before shared renderer use that can race.
- Work keys use book identity, section index, render config fingerprint, and cache path.
- Stale work writes no visible state.

## Memory and power rules

- Keep current PSRAM guards.
- Keep the 80 percent largest-block rule for optional PSRAM buffers.
- Keep producer work bounded by section and cache chunk.
- Keep task priority below input handling.
- Keep the producer off when no work is ready.
- Release the performance lock when the producer stops.
- Measure current draw before any always-on prefetch rule.

## Failure behavior

- Pinned task failure leaves reading usable.
- PSRAM failure keeps current fallbacks.
- SD failure stops producer work and keeps the last valid cache header.
- Parse failure marks only the active work item as failed.
- Low heap stops producer work.
- Stale layout work cannot replace a valid cache.

## Verification

Host checks:

- Run `make test`.
- Add one host check for default affinity and pinned affinity request handling.
- Run existing page cache checks.

Desktop checks:

- Build `reader-test`.
- Generate the torture EPUB cache in five-page batches.
- Dump the cache and check zone markers.

Device checks:

- Capture core traces on each S3 target.
- Capture timing tables on each S3 target.
- Compare first page, indexing, next-page, chapter jump, and layout change values.
- Check cancel paths with Back, section jump, font change, and book close.
- Check one low-memory run with images.

## Builder decisions

- Choose the torture book generator format.
- Choose the serial log parser format.
- Tune prefetch counts after baseline data.
- Tune chunk sizes only inside the stop latency budget.

## Calls made in this design

- Approach A is the first build target.
- Approach B waits for render-time data.
- Core 0 is the producer core on S3.
- No unpinned fallback is allowed for speed claims.
- The SD page cache stays.
- The torture EPUB is the main yardstick.
- Prefetch in approach A writes page records to SD only.
- Layout change uses a second parser and a staging cache path.
- The producer task is created pinned before float work runs.
- Phase speed gates use 20 percent median improvement on Pro and Classic and a 5 percent regression limit on all S3 targets.
- Sticky results stay separate until its display path is hardware-verified.
