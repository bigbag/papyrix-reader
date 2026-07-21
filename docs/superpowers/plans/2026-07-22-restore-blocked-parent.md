# Restore Blocked Parent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore trashed books to the root directory when their original parent is blocked by a file.

**Architecture:** Add a focused `Storage::isDirectory()` query so FileListState can distinguish an existing directory from another filesystem object. The existing `findRestorePath()` fallback logic then chooses a collision-free root destination when the original parent is unusable.

**Tech Stack:** C++17, Arduino-ESP32, SdFat, host CMake unit tests.

---

### Task 1: Add directory-type storage query

**Files:**
- Modify: `src/drivers/Storage.h`
- Modify: `src/drivers/Storage.cpp`

- [ ] **Step 1: Add the Storage API declaration**

Add this declaration after `exists()` in `src/drivers/Storage.h`:

```cpp
Result<bool> isDirectory(const char* path);
```

- [ ] **Step 2: Implement the query**

Add this implementation after `Storage::exists()` in `src/drivers/Storage.cpp`:

```cpp
Result<bool> Storage::isDirectory(const char* path) {
  if (!mounted_) {
    return Err<bool>(Error::SdCardNotFound);
  }
  if (!path || path[0] == '\0') {
    return Err<bool>(Error::InvalidOperation);
  }

  FsFile entry = SdMan.open(path);
  if (!entry) {
    return Err<bool>(Error::FileNotFound);
  }
  const bool directory = entry.isDirectory();
  entry.close();
  return Ok(directory);
}
```

- [ ] **Step 3: Build the firmware**

Run: `make build`

Expected: the firmware builds successfully.

- [ ] **Step 4: Commit the storage query**

```bash
git add src/drivers/Storage.h src/drivers/Storage.cpp
git commit -m "feat: identify storage directories"
```

### Task 2: Fall back when restore parent is not a directory

**Files:**
- Modify: `src/states/FileListState.cpp:307-311`

- [ ] **Step 1: Replace the restore parent readiness callback**

Replace the callback passed as the sixth argument to `trash::findRestorePath()` with:

```cpp
[&](const char* parent) {
  if (strcmp(parent, "/") == 0) return true;
  const auto exists = core.storage.exists(parent);
  if (!exists.ok()) return false;
  if (*exists) {
    const auto directory = core.storage.isDirectory(parent);
    return directory.ok() && *directory;
  }
  return core.storage.mkdir(parent).ok();
},
```

This retains nested-parent creation, while reporting a file blocker as unavailable so `findRestorePath()` uses its root fallback.

- [ ] **Step 2: Build the firmware**

Run: `make build`

Expected: the firmware builds successfully.

- [ ] **Step 3: Commit the restore behavior**

```bash
git add src/states/FileListState.cpp
git commit -m "fix: fall back when trash restore parent is a file"
```

### Task 3: Add focused regression coverage

**Files:**
- Modify: `test/unit/trash/TrashPathsTest.cpp`

- [ ] **Step 1: Add a failing blocked-parent regression test**

After the existing `missingParent` test setup, add a parent callback that represents a path occupied by a file and assert root fallback:

```cpp
const auto blockedParent = [](const char*) { return false; };
t.expectTrue(papyrix::trash::findRestorePath(path, sizeof(path), restoreParent, sizeof(restoreParent),
                                              "/trash/books/Book.epub", "Book.epub", blockedParent, rootExists),
             "falls back to root when original parent is blocked by a file");
t.expectEqual(std::string(path), std::string("/Book (2).epub"),
              "blocked parent uses root collision suffixing");
```

- [ ] **Step 2: Run the focused test**

Run: `make test-build && test/build/bin/TrashPathsTest`

Expected: `Trash Paths` reports all tests passed, including the blocked-parent fallback assertions.

- [ ] **Step 3: Run the complete test suite**

Run: `make test`

Expected: all test suites pass.

- [ ] **Step 4: Commit the regression test**

```bash
git add test/unit/trash/TrashPathsTest.cpp
git commit -m "test: cover blocked trash restore parent"
```
