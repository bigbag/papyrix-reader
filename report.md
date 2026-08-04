# Witchhunt Reader → Papyrix: Reuse Assessment

Comparison of `witchhunt-reader/` against Papyrix for features, concepts, and techniques worth adopting.

**Source:** `witchhunt-reader/` (CrossPoint lineage, X3/X4 ESP32-C3 e-reader firmware)
**Target:** Papyrix reader
**Date:** 2026-08-04

---

## Summary

Yes — there is useful material in Witchhunt Reader, but mostly as **ideas and targeted techniques**, not a wholesale port. Witch is optimized hard for speed, CSS layout, and memory. Papyrix already wins on languages (CJK/Arabic/Thai), FB2, Knuth–Plass, themes, and dual-mode architecture.

**Bottom line:** Steal **memory patterns**, **footnote cache design**, **richer CSS pieces**, **KOSync**, and **gesture/stats UX**. Treat Witch as a design reference and test oracle — not something to merge wholesale.

---

## Already covered / low value to copy

| Witch feature | Papyrix status |
|---|---|
| EPUB/MD/TXT, hyphenation, AA | Have it |
| Bookmarks, recent books, TOC | Have it |
| Custom fonts/themes, sleep screens | Have it |
| Background page cache | Have it (`PageCache` + `BackgroundTask`) |
| Button remap | Have it (basic) |
| Calibre / web upload | Have it |
| Natural sort | Have it |
| Clock | Have mini-app `ClockApp` |
| OPDS | Removed on purpose |
| ActivityManager stack | Different UI model; stick with `State` |

---

## Worth borrowing (ranked)

### High value — reading quality

#### 1. Richer CSS / layout

- **Witch:** floats (text wrap), real tables, small-caps, strikethrough, CSS `line-height` / `font-size`, drop caps.
- **Papyrix today:** tables are `[Table omitted]` with an explicit TODO; CSS is simpler; complex layouts are sanitized.
- **Takeaway:** don’t port the whole engine. Steal the *property model* (`CssFloat`, `CssTextDecoration`, `lineHeight`, `fontSizeMultiplier`) and implement tables/floats incrementally. Highest user-visible win for commercial EPUBs.

**Reference:** `witchhunt-reader/lib/Epub/Epub/css/CssStyle.h`

#### 2. Footnote system (`FootnotePreviews`)

- Book-level `footnotes.bin`, hash index on SD, inline previews.
- Handles Calibre/MOBI-style notes — not only EPUB3 `epub:type`.
- **Takeaway:** concept + on-disk format is gold; fits Papyrix’s SD-cache style.

**Reference:** `witchhunt-reader/lib/Epub/Epub/FootnotePreviews.h`

#### 3. KOReader sync

- Witch has a full on-device KOSync client.
- Papyrix docs mention it historically; no `lib/KOReaderSync` in tree now.
- **Takeaway:** high multi-device value if progress sync is a goal. Prefer protocol reimplementation over blind copy (shared ancestry / attribution).

**Reference:** `witchhunt-reader/lib/KOReaderSync/`

---

### High value — memory / speed concepts

#### 4. `BuildArena` bump allocator

- One budgeted arena for section build → no mid-parse heap fragmentation OOMs.
- **Takeaway:** excellent fit for ESP32-C3; pairs with Papyrix’s “check largest free block” discipline. Small, portable idea.

**Reference:** `witchhunt-reader/lib/Memory/BuildArena.h`

#### 5. Secondary framebuffer release during heavy work

- Temporarily free ~48KB AA buffer while indexing, restore after.
- **Takeaway:** technique, not a library. Big for large/CSS-heavy books.

**Reference:** `witchhunt-reader/docs/secondary-buffer-management.md`

#### 6. Background section lookahead (A/B/C model)

- Not just page pre-render: idle pre-build of next ~3 spines, cooperative slices, heap gates, AA deferred pass priority.
- **Takeaway:** evolve `ReaderState` background caching toward “next chapter ready before you hit it.”

**Reference:** `witchhunt-reader/docs/background-rendering.md`

#### 7. TJpgDec (+ progressive JPEG path)

- Witch replaced heavier JPEG stacks with IRAM-friendly TJpgDec; big win on covers/images.
- **Takeaway:** evaluate vs `picojpeg` if image path is still slow/fragile.

**Reference:** `witchhunt-reader/lib/TJpgDec/`

---

### Medium value — UX

#### 8. `ButtonEventManager` (short / double / long → actions)

- 23 actions × gesture; double-click only adds latency when configured.
- Papyrix has long-press + power actions; not a full per-button gesture matrix.
- **Takeaway:** good QoL for power-user controls without UI chrome.

**Reference:** `witchhunt-reader/src/ButtonEventManager.h`

#### 9. Reading stats + session tracker

- Streaks, time, pages/min, ETA — small standalone modules (~350 LOC).
- **Takeaway:** easy “apps/” or Settings screen; optional.

**Reference:** `witchhunt-reader/src/ReadingStats.*`, `ReadingSessionTracker.*`

#### 10. Global + named bookmarks

- Cross-book jump index + custom names.
- Papyrix has per-book bookmarks only.

**Reference:** `witchhunt-reader/src/GlobalBookmarkIndex.*`, `BookmarkStore.h`

#### 11. Book info screen + finished-book flow

- Metadata/cover/description; series/next-book/move-folder on finish.

#### 12. Sleep screen upgrades

- Transparent overlay on current page, info strip (title/chapter/%), PNG alpha, sequential pick.
- Papyrix already has Dark/Light/Custom/Cover/Keep Page — extend, don’t replace.

#### 13. Large-directory support with an SD-backed `FileIndex` — adopted

- Papyrix now keeps directories with up to 128 matching entries in RAM and switches larger directories to an SD-backed index.
- Every matching entry remains accessible and naturally sorted while RAM usage stays independent of directory size.
- The implementation detects large directories early and releases the temporary in-memory entries before building or validating the index.
- File removal and recycle-bin behavior use the same indexed backend, including move, restore, permanent deletion, empty-directory deletion, duplicate-name handling, and trash-root protection.
- The design borrows Witch’s external-sort/index-file concept without its peak-memory flaw: Witch still builds and retains the complete in-memory list before opening its index.

**References:** `lib/FileIndex/`, `witchhunt-reader/lib/FileIndex/`

#### 14. USB serial file transfer (MicroReader-compatible)

- Nice for Calibre plugin / file managers without WiFi.
- Medium effort; clear product niche.

**Reference:** `witchhunt-reader/lib/SerialTransfer/`, `src/SerialTransferDevice.*`

#### 15. Captive-portal client detect + QR

- Practical for hotel/cafe WiFi.
- Fits network stack if that UX matters.

---

### Lower fit / be careful

| Item | Why |
|---|---|
| **Weather panel** | Fun; optional mini-app only. Not core reading. |
| **GIF decoder** | Niche in books. |
| **Bionic/focus reading** | Polarizing; easy later if layout supports span styles. |
| **Cover carousel home** | Polish; Home/Recent already work. |
| **I18n UI strings system** | Papyrix has `lib/I18n`; compare docs, don’t replace blindly. |
| **yxml SaxParser vs Expat** | Memory tradeoff research only; big rip-up. |
| **ActivityManager** | Architectural fork; high cost, low need. |

---

## Practical shortlist (max benefit)

If the goal is max benefit for Papyrix without becoming Witch:

1. **`BuildArena` + secondary-buffer release** — reliability on big books
2. **Tables + strikethrough + basic float images** — close own TODOs / CSS gap
3. **Footnote preview cache** — reading UX
4. **Background next-section pre-build** — fewer “Indexing…” stalls
5. **KOReader sync** — if multi-device is a goal
6. **Button short/double/long actions** — power users
7. **Reading stats** — cheap delight

---

## What not to do

- Don’t vendor large chunks of Witch UI/activity code — different architecture, messy history/attribution (they even rewrote SerialTransfer clean-room for that reason).
- Don’t re-add OPDS unless product direction changed (it was intentionally removed).
- Don’t trade away CJK/RTL/FB2 for their CSS wins — merge ideas into *Papyrix* pipeline (`ChapterHtmlSlimParser` / `PageCache`).

---

## Papyrix strengths to keep

- FB2 support
- CJK / Arabic / Thai / Vietnamese text
- Knuth–Plass line breaking
- Custom themes from SD
- Dual-boot / reader-mode memory strategy
- Cleaner State-based UI vs Activity stack
- Focused scope (reader-first)

---

## Key Witch paths for follow-up

| Area | Path |
|---|---|
| Feature matrix / README | `witchhunt-reader/README.md` |
| CSS model | `witchhunt-reader/lib/Epub/Epub/css/CssStyle.h` |
| Footnotes | `witchhunt-reader/lib/Epub/Epub/FootnotePreviews.h` |
| Build arena | `witchhunt-reader/lib/Memory/BuildArena.h` |
| Background render | `witchhunt-reader/docs/background-rendering.md` |
| Secondary buffer | `witchhunt-reader/docs/secondary-buffer-management.md` |
| Button gestures | `witchhunt-reader/src/ButtonEventManager.h` |
| Reading stats | `witchhunt-reader/src/ReadingStats.*` |
| Global bookmarks | `witchhunt-reader/src/GlobalBookmarkIndex.*` |
| File index | `witchhunt-reader/lib/FileIndex/` |
| KOReader sync | `witchhunt-reader/lib/KOReaderSync/` |
| Serial transfer | `witchhunt-reader/lib/SerialTransfer/` |
| JPEG decoder | `witchhunt-reader/lib/TJpgDec/` |
