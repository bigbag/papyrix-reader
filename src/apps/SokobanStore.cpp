#include "SokobanStore.h"

#include <CheckedFile.h>
#include <SdFat.h>

#include <cstring>

#include "../hal/Storage.h"

namespace papyrix::sokoban {
namespace {
constexpr const char* kPath = "/.papyrix/apps/sokoban.bin";
constexpr const char* kTemporaryPath = "/.papyrix/apps/sokoban.bin.tmp";
constexpr const char* kBackupPath = "/.papyrix/apps/sokoban.bin.bak";
constexpr size_t kRecordSize = 26 + 3 * kUndoLimit;

bool removeIfPresent(hal::Storage& storage, const char* path) {
  const auto exists = storage.exists(path);
  return exists.ok() && (!exists.value || storage.remove(path).ok());
}

LoadResult readRecord(hal::Storage& storage, const char* path, Game& out) {
  const auto exists = storage.exists(path);
  if (!exists.ok()) return LoadResult::IOError;
  if (!exists.value) return LoadResult::Missing;
  FsFile file;
  if (!storage.openRead(path, file).ok()) return LoadResult::IOError;
  if (file.fileSize() != kRecordSize) {
    return file.close() ? LoadResult::Invalid : LoadResult::IOError;
  }
  uint8_t record[kRecordSize];
  const int length = file.read(record, sizeof(record));
  const bool closed = file.close();
  if (length != int(sizeof(record)) || !closed) return LoadResult::IOError;
  if (std::memcmp(record, "PXSK", 4) || record[4] != 1) return LoadResult::Invalid;
  Game value;
  value.level = record[5];
  value.player = record[6];
  value.boxCount = record[7];
  for (unsigned i = 0; i < kMaxBoxes; ++i) value.boxes[i] = record[8 + i];
  for (unsigned i = 0; i < 4; ++i) {
    value.moves |= uint32_t(record[16 + i]) << (8 * i);
    value.pushes |= uint32_t(record[20 + i]) << (8 * i);
  }
  value.undoCount = record[24];
  value.undoNext = record[25];
  for (unsigned i = 0; i < kUndoLimit; ++i) {
    const unsigned offset = 26 + 3 * i;
    if (record[offset + 2] > 3) return LoadResult::Invalid;
    value.history[i] = {record[offset], record[offset + 1], bool(record[offset + 2] & 1), bool(record[offset + 2] & 2)};
  }
  if (!valid(value)) return LoadResult::Invalid;
  out = value;
  return LoadResult::Loaded;
}
}  // namespace

bool clear(hal::Storage& storage) {
  return removeIfPresent(storage, kTemporaryPath) && removeIfPresent(storage, kBackupPath) &&
         removeIfPresent(storage, kPath);
}

LoadResult load(hal::Storage& storage, Game& out) {
  Game value;
  auto result = readRecord(storage, kPath, value);
  if (result != LoadResult::Loaded) {
    const auto backup = readRecord(storage, kBackupPath, value);
    if (backup == LoadResult::Loaded)
      result = backup;
    else if (result == LoadResult::Missing || backup == LoadResult::IOError)
      result = backup;
  }
  if (result == LoadResult::Loaded) out = value;
  return result;
}

bool save(hal::Storage& storage, const Game& game) {
  if (!valid(game)) return false;
  const auto directory = storage.exists("/.papyrix/apps");
  if (!directory.ok() || (!directory.value && !storage.mkdir("/.papyrix/apps").ok())) return false;
  Game previous;
  auto final = readRecord(storage, kPath, previous);
  const auto backup = readRecord(storage, kBackupPath, previous);
  if (final == LoadResult::IOError || backup == LoadResult::IOError) return false;
  if (final != LoadResult::Loaded && backup == LoadResult::Loaded) {
    if (!removeIfPresent(storage, kPath) || !storage.rename(kBackupPath, kPath).ok()) return false;
    final = LoadResult::Loaded;
  } else {
    if (final == LoadResult::Invalid || (final == LoadResult::Missing && backup == LoadResult::Invalid)) return false;
    if (!removeIfPresent(storage, kBackupPath)) return false;
  }
  uint8_t record[kRecordSize] = {'P', 'X', 'S', 'K', 1};
  record[5] = game.level;
  record[6] = game.player;
  record[7] = game.boxCount;
  for (unsigned i = 0; i < kMaxBoxes; ++i) record[8 + i] = game.boxes[i];
  for (unsigned i = 0; i < 4; ++i) {
    record[16 + i] = static_cast<uint8_t>(game.moves >> (8 * i));
    record[20 + i] = static_cast<uint8_t>(game.pushes >> (8 * i));
  }
  record[24] = game.undoCount;
  record[25] = game.undoNext;
  for (unsigned i = 0; i < kUndoLimit; ++i) {
    const unsigned offset = 26 + 3 * i;
    record[offset] = game.history[i].player;
    record[offset + 1] = game.history[i].box;
    record[offset + 2] = (game.history[i].countedMove ? 1 : 0) | (game.history[i].countedPush ? 2 : 0);
  }
  return miniapps::publishCheckedFile<FsFile>(
      storage, kPath, kTemporaryPath, kBackupPath, final == LoadResult::Loaded,
      [&](FsFile& file) { return file.write(record, sizeof(record)) == sizeof(record); });
}
}  // namespace papyrix::sokoban
