#pragma once

#include <strings.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace papyrix::trash {

inline constexpr char DIRECTORY[] = "/trash";
inline constexpr unsigned MAX_SUFFIX = 9999;

enum class DeleteAction : uint8_t {
  MoveToTrash,
  PermanentlyDelete,
  DeleteEmptyDirectory,
};

// Result of probing whether a candidate path can be used.
enum class PathProbe : uint8_t {
  Vacant,
  Occupied,
  Failed,  // I/O or lookup error — abort search immediately
};

inline DeleteAction deleteAction(bool isDirectory, bool isTrashPath) {
  if (isDirectory) return DeleteAction::DeleteEmptyDirectory;
  return isTrashPath ? DeleteAction::PermanentlyDelete : DeleteAction::MoveToTrash;
}

inline bool isDirectory(const char* path) { return path && strcasecmp(path, DIRECTORY) == 0; }

inline bool isDirectoryName(const char* name) { return name && strcasecmp(name, DIRECTORY + 1) == 0; }

inline bool isPath(const char* path) {
  constexpr size_t directoryLen = sizeof(DIRECTORY) - 1;
  return path && strncasecmp(path, DIRECTORY, directoryLen) == 0 &&
         (path[directoryLen] == '\0' || path[directoryLen] == '/');
}

inline bool buildTrashParent(char* out, size_t outSize, const char* sourcePath) {
  if (!out || outSize == 0 || !sourcePath || sourcePath[0] != '/' || sourcePath[1] == '\0') return false;

  const char* slash = strrchr(sourcePath, '/');
  int written;
  if (slash == sourcePath) {
    written = snprintf(out, outSize, "%s", DIRECTORY);
  } else {
    written = snprintf(out, outSize, "%s%.*s", DIRECTORY, static_cast<int>(slash - sourcePath), sourcePath);
  }
  return written >= 0 && static_cast<size_t>(written) < outSize;
}

inline bool buildSourceParent(char* out, size_t outSize, const char* path) {
  if (!out || outSize == 0 || !path || path[0] != '/' || path[1] == '\0') return false;

  const char* slash = strrchr(path, '/');
  const int written = slash == path ? snprintf(out, outSize, "/")
                                    : snprintf(out, outSize, "%.*s", static_cast<int>(slash - path), path);
  return written >= 0 && static_cast<size_t>(written) < outSize;
}

inline bool buildRestoreParent(char* out, size_t outSize, const char* trashedPath) {
  if (!out || outSize == 0 || !isPath(trashedPath) || isDirectory(trashedPath)) return false;

  const char* relativePath = trashedPath + sizeof(DIRECTORY) - 1;
  if (relativePath[0] != '/' || relativePath[1] == '\0') return false;

  const char* slash = strrchr(relativePath, '/');
  int written;
  if (slash == relativePath) {
    written = snprintf(out, outSize, "/");
  } else {
    written = snprintf(out, outSize, "%.*s", static_cast<int>(slash - relativePath), relativePath);
  }
  return written >= 0 && static_cast<size_t>(written) < outSize;
}

inline bool buildCandidate(char* out, size_t outSize, const char* directory, const char* filename, unsigned suffix) {
  if (!out || outSize == 0 || !directory || directory[0] == '\0' || !filename || filename[0] == '\0' || suffix == 0)
    return false;

  // Strip trailing slashes so joining never produces a double slash ("/" -> "/name", not "//name").
  size_t dirLen = strlen(directory);
  while (dirLen > 0 && directory[dirLen - 1] == '/') dirLen--;

  const char* extension = strrchr(filename, '.');
  const bool hasExtension = extension && extension != filename;
  int written;
  if (suffix == 1) {
    written = snprintf(out, outSize, "%.*s/%s", static_cast<int>(dirLen), directory, filename);
  } else if (hasExtension) {
    written = snprintf(out, outSize, "%.*s/%.*s (%u)%s", static_cast<int>(dirLen), directory,
                       static_cast<int>(extension - filename), filename, suffix, extension);
  } else {
    written = snprintf(out, outSize, "%.*s/%s (%u)", static_cast<int>(dirLen), directory, filename, suffix);
  }
  return written >= 0 && static_cast<size_t>(written) < outSize;
}

template <typename Probe>
bool findVacantPath(char* out, size_t outSize, const char* directory, const char* filename, Probe&& probe) {
  for (unsigned suffix = 1; suffix <= MAX_SUFFIX; suffix++) {
    if (!buildCandidate(out, outSize, directory, filename, suffix)) return false;
    switch (probe(out)) {
      case PathProbe::Vacant:
        return true;
      case PathProbe::Occupied:
        break;
      case PathProbe::Failed:
        return false;
    }
  }
  return false;
}

template <typename ParentReady, typename Probe>
bool findRestorePath(char* out, size_t outSize, char* parent, size_t parentSize, const char* trashedPath,
                     const char* filename, ParentReady&& parentReady, Probe&& probe) {
  if (!buildRestoreParent(parent, parentSize, trashedPath)) return false;

  if (parentReady(parent)) {
    return findVacantPath(out, outSize, parent, filename, probe);
  }
  if (strcmp(parent, "/") == 0) return false;
  return findVacantPath(out, outSize, "/", filename, probe);
}

}  // namespace papyrix::trash
