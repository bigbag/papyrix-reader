# Long Path Support Design

## Goal

Support book paths up to 1023 UTF-8 bytes throughout Papyrix: File Manager navigation, reader boot handoff, persisted settings, content opening, and recycle-bin moves/restores.

## Bounds

`BufferSize::FilePath` will be 1024 bytes, including the NUL terminator. It is the maximum source/book-path capacity.

Recycle-bin paths need a separate `BufferSize::TrashPath` of 1037 bytes. A source path can occupy 1023 bytes; placing it below `/trash` adds six bytes, and a collision can add ` (9999)` (seven bytes). The final byte is the NUL terminator.

Ordinary File Manager navigation and reader entry reject paths that exceed `FilePath`. Paths inside `/trash` may use `TrashPath`, allowing every item produced by a valid move to be browsed and restored.

## Data Flow

The 1024-byte source-path capacity will be used by:

- `Core::Buffers::path`
- `ModeTransition::bookPath` in RTC memory
- `Settings::lastBookPath`
- `ReaderState::contentPath_`
- File Manager source-path construction

`Settings::fileListDir`, File Manager current/selected paths, and the single action destination buffer will use `TrashPath`, so returning from Reader mode and navigating nested trash paths do not truncate them.

`HomeView::bookPath` remains 128 bytes because it is display-only. Continuing a book uses `core.buf.path`, which receives the full path.

## File Manager Action Storage

Confirmation actions must not allocate several one-kilobyte local arrays on the FreeRTOS stack. FileListState will reuse its selected/current path members for the source and temporary parent, and retain one state-owned `TrashPath` destination buffer. After an action, it reconstructs the original browsing directory from the selected source path before reloading entries.

## Settings Migration

The settings file version will increase. Version 12 and older fields are read at their historical widths and zero-extended into the enlarged buffers:

- versions 10 and older: 256-byte `lastBookPath` and `fileListDir`
- versions 11–12: 512-byte `lastBookPath` and `fileListDir`
- new version: 1024-byte `lastBookPath` and 1037-byte `fileListDir`

Both normal Storage-backed and early-boot SdMan-backed load/save paths follow the same migration logic. The settings field count is unchanged because no serialized field is added or reordered.

## Validation

Tests will verify the 1023-byte path handoff chain, migration from a version-12 settings file, maximum trash path construction including the ` (9999)` suffix, and locale example override usage measured as UTF-8 value bytes plus one NUL per loaded value against the 4096-byte I18n buffer.

`make test`, `make check`, and a firmware build will be run. Device verification should browse, open, move, restore, and permanently delete a near-limit path, then reopen the device to confirm the persisted last-book path and File Manager directory remain intact.
