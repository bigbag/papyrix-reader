# Restore Blocked Parent Design

## Goal

Restore a trashed book to the SD-card root when its original parent path is occupied by a file rather than a directory.

## Design

Add a small storage query that reports whether a path is an existing directory. During restore, the parent-ready callback will:

1. Treat `/` as ready.
2. If the original parent exists, accept it only when it is a directory.
3. If it does not exist, create it and accept it when creation succeeds.
4. Otherwise report it unavailable, allowing `findRestorePath()` to select a collision-free root destination.

This avoids retrying a failed rename, so real I/O failures still produce the restore-failed message rather than silently relocating the book.

## Tests

Extend the restore-path behavior coverage to verify that a blocked original parent causes root fallback and retains normal root collision suffixing.
