#include "Game2048Store.h"

#include <CheckedFile.h>
#include <SdFat.h>

#include <cstring>

#include "../hal/Storage.h"

namespace papyrix::game2048 {
namespace {
constexpr const char* kPath = "/.papyrix/apps/2048.bin";
constexpr const char* kTemporaryPath = "/.papyrix/apps/2048.bin.tmp";
constexpr const char* kBackupPath = "/.papyrix/apps/2048.bin.bak";
constexpr size_t kRecordSize = 26;

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
    file.close();
    return LoadResult::Invalid;
  }
  uint8_t record[kRecordSize];
  const int length = file.read(record, sizeof(record));
  const bool closed = file.close();
  if (length != static_cast<int>(sizeof(record)) || !closed) return LoadResult::IOError;
  if (std::memcmp(record, "PX48", 4) || record[4] != 1 || record[25] > 1) return LoadResult::Invalid;
  Game value;
  std::memcpy(value.cells, record + 5, sizeof(value.cells));
  for (unsigned i = 0; i < 4; ++i) value.score |= uint32_t(record[21 + i]) << (8 * i);
  value.wonAcknowledged = record[25] != 0;
  unsigned occupied = 0;
  for (const auto cell : value.cells)
    if (cell) ++occupied;
  if (occupied < 2 || !valid(value)) return LoadResult::Invalid;
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
  if (result != LoadResult::Loaded) return result;
  if (isStuck(value)) return clear(storage) ? LoadResult::Missing : LoadResult::IOError;
  out = value;
  return LoadResult::Loaded;
}

bool save(hal::Storage& storage, const Game& game) {
  unsigned occupied = 0;
  for (const auto cell : game.cells)
    if (cell) ++occupied;
  if (occupied < 2 || !valid(game) || isStuck(game)) return false;
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
  uint8_t record[kRecordSize] = {'P', 'X', '4', '8', 1};
  std::memcpy(record + 5, game.cells, sizeof(game.cells));
  for (unsigned i = 0; i < 4; ++i) record[21 + i] = static_cast<uint8_t>(game.score >> (8 * i));
  record[25] = game.wonAcknowledged ? 1 : 0;
  return miniapps::publishCheckedFile<FsFile>(
      storage, kPath, kTemporaryPath, kBackupPath, final == LoadResult::Loaded,
      [&](FsFile& file) { return file.write(record, sizeof(record)) == sizeof(record); });
}
}  // namespace papyrix::game2048
