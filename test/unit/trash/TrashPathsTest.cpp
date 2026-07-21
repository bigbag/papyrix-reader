#include "test_utils.h"

#include <TrashPaths.h>
#include <Types.h>

#include <set>
#include <string>

int main() {
  TestUtils::TestRunner t("Trash Paths");
  char path[64];

  t.expectTrue(papyrix::trash::isDirectory("/trash"), "recognizes trash directory");
  t.expectFalse(papyrix::trash::isDirectory("/trash/subdir"), "does not match child directory");
  t.expectTrue(papyrix::trash::isDirectory("/Trash"), "recognizes mixed-case trash directory");
  t.expectTrue(papyrix::trash::isDirectoryName("Trash"), "recognizes mixed-case trash root entry");
  t.expectTrue(papyrix::trash::isPath("/Trash/books/Book.epub"), "recognizes mixed-case trash descendant");
  t.expectFalse(papyrix::trash::isPath("/trash-old/Book.epub"), "does not match trash prefix");

  t.expectTrue(papyrix::trash::buildTrashParent(path, sizeof(path), "/books/scifi/Book.epub"),
               "builds nested trash parent");
  t.expectEqual(std::string(path), std::string("/trash/books/scifi"), "preserves original parent below trash");
  t.expectTrue(papyrix::trash::buildTrashParent(path, sizeof(path), "/Book.epub"), "builds root trash parent");
  t.expectEqual(std::string(path), std::string("/trash"), "root file uses trash root");

  t.expectTrue(papyrix::trash::buildRestoreParent(path, sizeof(path), "/Trash/books/scifi/Book.epub"),
               "builds nested restore parent");
  t.expectEqual(std::string(path), std::string("/books/scifi"), "restores nested original parent");
  t.expectTrue(papyrix::trash::buildRestoreParent(path, sizeof(path), "/trash/Book.epub"),
               "builds root restore parent");
  t.expectEqual(std::string(path), std::string("/"), "root trash file restores to root");
  t.expectFalse(papyrix::trash::buildRestoreParent(path, sizeof(path), "/books/Book.epub"),
                "rejects non-trash restore source");
  t.expectFalse(papyrix::trash::buildRestoreParent(path, sizeof(path), "/trash/"),
                "rejects trash directory without a file");

  const std::string maximumFilename = "b.epub";
  const std::string maximumSource =
      "/" + std::string(papyrix::BufferSize::FilePath - 1 - 2 - maximumFilename.size(), 'a') + "/" +
      maximumFilename;
  char maximumParent[papyrix::BufferSize::TrashPath];
  char maximumCandidate[papyrix::BufferSize::TrashPath];
  char restoredParent[papyrix::BufferSize::FilePath];
  t.expectEq(papyrix::BufferSize::FilePath - 1, maximumSource.size(), "maximum source length");
  t.expectTrue(papyrix::trash::buildTrashParent(maximumParent, sizeof(maximumParent), maximumSource.c_str()),
               "maximum source has a trash parent");
  t.expectTrue(papyrix::trash::buildCandidate(maximumCandidate, sizeof(maximumCandidate), maximumParent,
                                              maximumFilename.c_str(), 9999),
               "maximum collision destination fits");
  t.expectEq(papyrix::BufferSize::TrashPath - 1, strlen(maximumCandidate), "maximum destination length");
  t.expectFalse(papyrix::trash::buildCandidate(maximumCandidate, sizeof(maximumCandidate) - 1, maximumParent,
                                               maximumFilename.c_str(), 9999),
                "maximum collision destination rejects one-byte-short buffer");
  t.expectTrue(papyrix::trash::buildSourceParent(restoredParent, sizeof(restoredParent), maximumSource.c_str()),
               "reconstructs maximum source parent");

  t.expectTrue(papyrix::trash::buildCandidate(path, sizeof(path), "/trash", "Book.epub", 1),
               "builds first candidate");
  t.expectEqual(std::string(path), std::string("/trash/Book.epub"), "first candidate keeps name");

  t.expectTrue(papyrix::trash::buildCandidate(path, sizeof(path), "/trash", "Book.epub", 2),
               "builds suffixed candidate");
  t.expectEqual(std::string(path), std::string("/trash/Book (2).epub"), "suffix precedes extension");

  t.expectTrue(papyrix::trash::buildCandidate(path, sizeof(path), "/trash", "README", 3),
               "builds no-extension candidate");
  t.expectEqual(std::string(path), std::string("/trash/README (3)"), "suffixes no-extension name");

  using PathProbe = papyrix::trash::PathProbe;
  std::set<std::string> occupied{"/trash/Book.epub", "/trash/Book (2).epub"};
  const auto exists = [&occupied](const char* candidate) {
    return occupied.count(candidate) != 0 ? PathProbe::Occupied : PathProbe::Vacant;
  };
  t.expectTrue(papyrix::trash::findVacantPath(path, sizeof(path), "/trash", "Book.epub", exists),
               "finds a free collision suffix");
  t.expectEqual(std::string(path), std::string("/trash/Book (3).epub"), "selects first free suffix");

  unsigned probeCalls = 0;
  const auto failingProbe = [&probeCalls](const char*) {
    probeCalls++;
    return PathProbe::Failed;
  };
  t.expectFalse(papyrix::trash::findVacantPath(path, sizeof(path), "/trash", "Book.epub", failingProbe),
                "aborts vacant search on probe failure");
  t.expectEq(1u, probeCalls, "probe failure does not scan remaining suffixes");

  t.expectTrue(papyrix::trash::buildCandidate(path, sizeof(path), "/", "Book.epub", 1),
               "builds root candidate");
  t.expectEqual(std::string(path), std::string("/Book.epub"), "root candidate has no double slash");

  t.expectTrue(papyrix::trash::buildCandidate(path, sizeof(path), "/", "Book.epub", 2),
               "builds suffixed root candidate");
  t.expectEqual(std::string(path), std::string("/Book (2).epub"), "root suffix has no double slash");

  t.expectTrue(papyrix::trash::buildCandidate(path, sizeof(path), "/trash/", "Book.epub", 1),
               "builds candidate for trailing-slash directory");
  t.expectEqual(std::string(path), std::string("/trash/Book.epub"), "trailing slash is normalized");

  t.expectFalse(papyrix::trash::buildCandidate(path, sizeof(path), "", "Book.epub", 1), "rejects empty directory");

  char small[12];
  t.expectFalse(papyrix::trash::buildCandidate(small, sizeof(small), "/trash", "Book.epub", 1),
                "rejects truncated path");

  using DeleteAction = papyrix::trash::DeleteAction;
  t.expectEq(static_cast<int>(DeleteAction::MoveToTrash),
             static_cast<int>(papyrix::trash::deleteAction(false, false)), "non-trash file moves to trash");
  t.expectEq(static_cast<int>(DeleteAction::PermanentlyDelete),
             static_cast<int>(papyrix::trash::deleteAction(false, true)), "trash file is deleted permanently");
  t.expectEq(static_cast<int>(DeleteAction::DeleteEmptyDirectory),
             static_cast<int>(papyrix::trash::deleteAction(true, false)),
             "regular directory is removed only when empty");
  t.expectEq(static_cast<int>(DeleteAction::DeleteEmptyDirectory),
             static_cast<int>(papyrix::trash::deleteAction(true, true)),
             "trash directory is removed only when empty");

  char restoreParent[64];
  const auto missingParent = [](const char*) { return false; };
  t.expectTrue(papyrix::trash::findRestorePath(path, sizeof(path), restoreParent, sizeof(restoreParent),
                                                "/trash/books/Book.epub", "Book.epub", missingParent, exists),
               "falls back to root when original parent is unavailable");
  t.expectEqual(std::string(path), std::string("/Book.epub"), "restores to root only after parent failure");

  std::set<std::string> restoredOccupied{"/books/Book.epub", "/books/Book (2).epub"};
  const auto parentReady = [](const char* parent) { return std::string(parent) == "/books"; };
  const auto restoredExists = [&restoredOccupied](const char* candidate) {
    return restoredOccupied.count(candidate) != 0 ? PathProbe::Occupied : PathProbe::Vacant;
  };
  t.expectTrue(papyrix::trash::findRestorePath(path, sizeof(path), restoreParent, sizeof(restoreParent),
                                                "/trash/books/Book.epub", "Book.epub", parentReady, restoredExists),
               "finds collision suffix in ready original parent");
  t.expectEqual(std::string(path), std::string("/books/Book (3).epub"),
                "does not fall back while original parent is ready");

  std::set<std::string> rootOccupied{"/Book.epub"};
  const auto rootExists = [&rootOccupied](const char* candidate) {
    return rootOccupied.count(candidate) != 0 ? PathProbe::Occupied : PathProbe::Vacant;
  };
  t.expectTrue(papyrix::trash::findRestorePath(path, sizeof(path), restoreParent, sizeof(restoreParent),
                                                "/trash/books/Book.epub", "Book.epub", missingParent, rootExists),
               "suffixes collision in root fallback");
  t.expectEqual(std::string(path), std::string("/Book (2).epub"), "root fallback preserves collision handling");

  // FileListState reports a parent occupied by a file as unavailable.
  const auto blockedParent = [](const char*) { return false; };
  t.expectTrue(papyrix::trash::findRestorePath(path, sizeof(path), restoreParent, sizeof(restoreParent),
                                                "/trash/books/Book.epub", "Book.epub", blockedParent, rootExists),
               "falls back to root when original parent is blocked by a file");
  t.expectEqual(std::string(path), std::string("/Book (2).epub"),
                "blocked parent uses root collision suffixing");

  unsigned restoreProbeCalls = 0;
  const auto restoreFailingProbe = [&restoreProbeCalls](const char*) {
    restoreProbeCalls++;
    return PathProbe::Failed;
  };
  t.expectFalse(papyrix::trash::findRestorePath(path, sizeof(path), restoreParent, sizeof(restoreParent),
                                                 "/trash/books/Book.epub", "Book.epub", parentReady,
                                                 restoreFailingProbe),
                "aborts restore search on probe failure");
  t.expectEq(1u, restoreProbeCalls, "restore probe failure does not scan remaining suffixes");

  return t.allPassed() ? 0 : 1;
}
