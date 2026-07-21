# Long Path Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Support 1023-byte book paths throughout File Manager, reader handoff, settings persistence, and recycle-bin operations without adding large FreeRTOS stack allocations.

**Architecture:** `BufferSize::FilePath` becomes the 1024-byte source-path capacity. `BufferSize::TrashPath` is 1037 bytes so a moved path and the longest collision suffix fit. Reader and persistent source-path fields use `FilePath`; FileListState allows `TrashPath` only for recycle-bin traversal and reuses state-owned buffers for actions.

**Tech Stack:** C++17, Arduino/ESP32-C3, SdFat, PlatformIO, CMake unit tests, Python 3.

---

## Files

- Modify: `src/core/Types.h` — declare source and recycle-bin path capacities.
- Modify: `src/core/Core.h`, `src/core/BootMode.h`, `src/core/PapyrixSettings.h`, `src/states/ReaderState.h` — apply the source-path capacity across the reader handoff.
- Modify: `src/core/PapyrixSettings.cpp` — write new field widths and migrate v10, v11/v12, and current settings files correctly.
- Modify: `src/states/FileListState.h`, `src/states/FileListState.cpp` — use recycle-bin-sized state buffers and avoid multi-KB action stack frames.
- Modify: `src/core/TrashPaths.h` — expose source-parent reconstruction needed after FileList action scratch reuse.
- Modify: `test/unit/settings/LongPathBufferTest.cpp`, `test/unit/settings/SettingsPathMigrationTest.cpp`, `test/unit/trash/TrashPathsTest.cpp` — regression coverage for new bounds and persisted migration.
- Modify: `test/scripts/LocaleExamplesTest.py` — enforce I18n override capacity for example locales.

### Task 1: Define and test the shared path capacities

**Files:**
- Modify: `src/core/Types.h:55-65`
- Modify: `src/core/Core.h:34-39`
- Modify: `src/core/BootMode.h:20-27`
- Modify: `src/core/PapyrixSettings.h:78-88`
- Modify: `src/states/ReaderState.h:50-58`
- Modify: `test/unit/settings/LongPathBufferTest.cpp`

- [ ] **Step 1: Update the failing expectations for the shared source-path capacity**

Replace fixed 512-byte local arrays and the final assertion in `LongPathBufferTest.cpp` with `papyrix::BufferSize::FilePath`; construct a 1023-byte `.epub` path and assert it survives every copied handoff.

```cpp
constexpr size_t kPathCapacity = papyrix::BufferSize::FilePath;
char selectedPath[kPathCapacity] = {};
char lastBookPath[kPathCapacity] = {};
char bookPath[kPathCapacity] = {};
char bufPath[kPathCapacity] = {};
char contentPath[kPathCapacity] = {};

runner.expectEq(size_t(1024), size_t(papyrix::BufferSize::FilePath),
                "FilePath buffer is 1024 bytes");
```

- [ ] **Step 2: Run the test to confirm the current 512-byte capacity fails**

Run: `make test-build && test/build/bin/LongPathBufferTest`

Expected: the updated 1024-byte capacity assertion fails.

- [ ] **Step 3: Introduce explicit source and recycle-bin capacities**

In `Types.h`, define the capacities in one place. `TrashPath` must include the NUL terminator.

```cpp
namespace BufferSize {
constexpr size_t Path = 256;
constexpr size_t FilePath = 1024;
constexpr size_t TrashPath = FilePath + (sizeof("/trash") - 1) + (sizeof(" (9999)") - 1);
constexpr size_t Text = 512;
```

Use `BufferSize::FilePath` instead of literal `512` for all book/source handoff fields:

```cpp
char path[BufferSize::FilePath];          // Core::Buffers
char bookPath[BufferSize::FilePath];      // ModeTransition
char lastBookPath[BufferSize::FilePath];  // Settings
char contentPath_[BufferSize::FilePath];  // ReaderState
```

- [ ] **Step 4: Run the focused test**

Run: `make test-build && test/build/bin/LongPathBufferTest`

Expected: all handoff and capacity assertions pass.

- [ ] **Step 5: Commit the shared capacity change**

```bash
git add src/core/Types.h src/core/Core.h src/core/BootMode.h src/core/PapyrixSettings.h \
  src/states/ReaderState.h test/unit/settings/LongPathBufferTest.cpp
git commit -m "feat: extend book path capacity"
```

### Task 2: Migrate persisted settings path fields

**Files:**
- Modify: `src/core/PapyrixSettings.cpp:20-52, 188-215, 405-433`
- Modify: `test/unit/settings/SettingsPathMigrationTest.cpp`

- [ ] **Step 1: Add a version-12 migration regression test**

Extend the test fixture with a v12 layout containing 512-byte `lastBookPath` and `fileListDir`, and make `LoadedPaths` use production capacities:

```cpp
constexpr size_t V12_LAST_BOOK_PATH_SIZE = 512;
constexpr size_t V12_FILE_LIST_DIR_SIZE = 512;

struct LoadedPaths {
  char lastBookPath[papyrix::BufferSize::FilePath] = "";
  char fileListDir[papyrix::BufferSize::TrashPath] = "/";
  char fileListSelectedName[256] = "";
  uint16_t fileListSelectedIndex = 0;
  uint8_t pendingTransition = 0;
  uint8_t transitionReturnTo = 0;
  uint8_t sunlightFadingFix = 0;
  uint8_t frontButtonLayout = 0;
  uint8_t fullBookProcess = 0;
};
```

Write a 511-byte v12 path and directory, load them as a v12 file, and assert their content is preserved and all bytes after index 511 are zero.

- [ ] **Step 2: Run the migration test and confirm it fails before migration support exists**

Run: `make test-build && test/build/bin/SettingsPathMigrationTest`

Expected: the v12 fixture is misread because the test/production logic still reads the new field width for v12.

- [ ] **Step 3: Bump the settings version and zero-extend legacy field widths**

Set the file version to 13 without changing `SETTINGS_COUNT`. Update the version comment and make both `load()` and `loadFromFile()` read the correct historical width:

```cpp
constexpr uint8_t SETTINGS_FILE_VERSION = 13;

if (version <= 10) {
  inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), 256);
  memset(lastBookPath + 256, 0, sizeof(lastBookPath) - 256);
} else if (version <= 12) {
  inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), 512);
  memset(lastBookPath + 512, 0, sizeof(lastBookPath) - 512);
} else {
  inputFile.read(reinterpret_cast<uint8_t*>(lastBookPath), sizeof(lastBookPath));
}
```

Apply the same three-way branch to `fileListDir` in both load functions. Save functions already use `sizeof(field)`, and `kMinSettingsBytes` automatically follows the changed members.

- [ ] **Step 4: Run the migration test**

Run: `make test-build && test/build/bin/SettingsPathMigrationTest`

Expected: v10, v11/v12, and current-format cases pass without field bleed.

- [ ] **Step 5: Commit the migration**

```bash
git add src/core/PapyrixSettings.cpp test/unit/settings/SettingsPathMigrationTest.cpp
git commit -m "feat: migrate settings to extended paths"
```

### Task 3: Make recycle-bin construction fit the declared bounds

**Files:**
- Modify: `src/core/TrashPaths.h:43-121`
- Modify: `test/unit/trash/TrashPathsTest.cpp`

- [ ] **Step 1: Add maximum-length trash destination tests**

Create a 1023-byte source path ending in `.epub`, build its trash parent and suffix-9999 candidate into a `BufferSize::TrashPath` buffer, and verify both succeed. Repeat with one byte less capacity and expect failure.

```cpp
const std::string filename = "b.epub";
const std::string source = "/" +
                           std::string(papyrix::BufferSize::FilePath - 1 - 2 - filename.size(), 'a') +
                           "/" + filename;
char parent[papyrix::BufferSize::TrashPath];
char candidate[papyrix::BufferSize::TrashPath];
char restored[papyrix::BufferSize::FilePath];

// source.size() is exactly BufferSize::FilePath - 1.
t.expectEq(papyrix::BufferSize::FilePath - 1, source.size(), "maximum source length");
t.expectTrue(papyrix::trash::buildTrashParent(parent, sizeof(parent), source.c_str()),
             "maximum source has a trash parent");
t.expectTrue(papyrix::trash::buildCandidate(candidate, sizeof(candidate), parent, filename.c_str(), 9999),
             "maximum collision destination fits");
t.expectEq(papyrix::BufferSize::TrashPath - 1, strlen(candidate), "maximum destination length");
t.expectFalse(papyrix::trash::buildCandidate(candidate, sizeof(candidate) - 1, parent, filename.c_str(), 9999),
              "maximum collision destination rejects one-byte-short buffer");
t.expectTrue(papyrix::trash::buildSourceParent(restored, sizeof(restored), source.c_str()),
             "reconstructs maximum source parent");
```

- [ ] **Step 2: Run the trash helper test and confirm it fails**

Run: `make test-build && test/build/bin/TrashPathsTest`

Expected: compilation fails because `buildSourceParent()` does not yet exist.

- [ ] **Step 3: Add source-parent reconstruction helper**

Add a helper that writes the parent of a non-root absolute path. It lets FileListState restore its browsing directory after reusing `currentDir_` as action scratch storage.

```cpp
inline bool buildSourceParent(char* out, size_t outSize, const char* path) {
  if (!out || outSize == 0 || !path || path[0] != '/' || path[1] == '\0') return false;
  const char* slash = strrchr(path, '/');
  const int written = slash == path ? snprintf(out, outSize, "/")
                                    : snprintf(out, outSize, "%.*s", static_cast<int>(slash - path), path);
  return written >= 0 && static_cast<size_t>(written) < outSize;
}
```

- [ ] **Step 4: Run the focused helper test**

Run: `make test-build && test/build/bin/TrashPathsTest`

Expected: existing collision tests and new maximum-bound tests pass.

- [ ] **Step 5: Commit the helper and boundary tests**

```bash
git add src/core/TrashPaths.h test/unit/trash/TrashPathsTest.cpp
git commit -m "test: cover recycle bin path limits"
```

### Task 4: Apply bounded path handling in FileListState

**Files:**
- Modify: `src/states/FileListState.h:38-70`
- Modify: `src/states/FileListState.cpp:207-340, 532-570`

- [ ] **Step 1: Replace fixed FileListState path arrays with declared capacities**

Use trash capacity for state that must represent a browse location inside `/trash`, and retain one state-owned destination buffer:

```cpp
char currentDir_[BufferSize::TrashPath];
char selectedPath_[BufferSize::TrashPath];
char actionDestination_[BufferSize::TrashPath];
```

Include `../core/Types.h` in the header if it is not already transitively available.

- [ ] **Step 2: Make selected-path construction enforce the applicable limit**

Change the helper signature to accept a capacity and pass `BufferSize::FilePath` for ordinary paths and `BufferSize::TrashPath` for trash paths:

```cpp
bool buildSelectedPath(char* path, size_t pathSize) const;

const size_t pathSize = isTrashDirectory() ? BufferSize::TrashPath : BufferSize::FilePath;
if (!buildSelectedPath(selectedPath_, pathSize)) return;
```

Use this same conditional source limit in `executeConfirmedAction`. This prevents a normal filesystem path larger than the reader’s supported source bound from becoming a truncation-prone handoff.

- [ ] **Step 3: Reuse members instead of allocating action-local path arrays**

At the start of `executeConfirmedAction`, build the source into `selectedPath_`. For a move, write the trash parent into `currentDir_`, build the collision candidate in `actionDestination_`, and rename `selectedPath_` to `actionDestination_`. For a restore, write the restore parent into `currentDir_`, find the candidate in `actionDestination_`, and rename likewise.

Before `loadFiles(core)`, restore the original browsing directory from `selectedPath_`:

```cpp
if (!trash::buildSourceParent(currentDir_, sizeof(currentDir_), selectedPath_)) {
  strcpy(currentDir_, "/");
}
loadFiles(core);
```

For ordinary move/delete actions this restores the normal source directory; for restore/permanent-delete it restores the trash directory containing the selected item.

- [ ] **Step 4: Build firmware and run focused tests**

Run: `make test-build && test/build/bin/TrashPathsTest && test/build/bin/LongPathBufferTest && make build`

Expected: tests pass and firmware builds without stack or compile errors.

- [ ] **Step 5: Commit File Manager support**

```bash
git add src/states/FileListState.h src/states/FileListState.cpp
git commit -m "feat: support long paths in file manager"
```

### Task 5: Enforce locale override-buffer capacity in example validation

**Files:**
- Modify: `test/scripts/LocaleExamplesTest.py:15-42`

- [ ] **Step 1: Add a failing capacity check**

Make the parser return both the key set and exact I18n buffer consumption. Count every non-metadata key/value assignment as `len(value.encode("utf-8")) + 1`, matching `I18n::setOverride()`.

```python
BUFFER_SIZE = 4096

def locale_data(path: Path) -> tuple[set[str], int]:
    keys: set[str] = set()
    used = 0
    for line in path.read_text().splitlines():
        if not line or line.startswith(("#", ";")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if not key.startswith("_"):
            keys.add(key)
            used += len(value.encode("utf-8")) + 1
    return keys, used
```

- [ ] **Step 2: Report and fail capacity violations**

In `main()`, add the capacity check beside missing/unexpected keys:

```python
actual, used = locale_data(locale)
capacity_exceeded = used > BUFFER_SIZE
if missing or unexpected or capacity_exceeded:
    failed = True
    # Existing key diagnostics.
    if capacity_exceeded:
        print(f"  override bytes: {used}/{BUFFER_SIZE}")
```

Include `used` in successful output so locale growth is visible.

- [ ] **Step 3: Run locale validation**

Run: `python3 test/scripts/LocaleExamplesTest.py`

Expected: every locale passes and reports usage at or below `4096`.

- [ ] **Step 4: Commit locale validation**

```bash
git add test/scripts/LocaleExamplesTest.py
git commit -m "test: enforce locale override capacity"
```

### Task 6: Final verification

**Files:**
- No source changes expected.

- [ ] **Step 1: Format changed C++ and Python-adjacent source**

Run: `make format`

Expected: formatter completes without modifying unrelated files. Revert unrelated formatter output if any appears.

- [ ] **Step 2: Run the complete automated suite**

Run: `make test && make check && make build`

Expected: all unit suites pass, static analysis completes, and the firmware builds.

- [ ] **Step 3: Inspect the final patch**

Run: `git diff --check HEAD~5..HEAD && git status --short`

Expected: no whitespace errors; only the intentionally untracked `crosspoint-reader/` directory remains outside commits.

- [ ] **Step 4: Perform device verification when hardware is available**

Create a nested path whose source is 1023 bytes including `.epub`. In File Manager: enter its directory, open the book, restart/return to verify the persisted reader and File Manager paths, move it to trash, browse to it, restore it, then permanently delete it. Verify each operation succeeds and the device remains stable.
