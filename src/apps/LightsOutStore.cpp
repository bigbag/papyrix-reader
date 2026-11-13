#include "LightsOutStore.h"

#include <CheckedFile.h>
#include <SdFat.h>

#include <cstring>

#include "../hal/Storage.h"

namespace papyrix::lightsout {
namespace {
constexpr const char* kPath = "/.papyrix/apps/lightsout.bin";
constexpr const char* kTemporaryPath = "/.papyrix/apps/lightsout.bin.tmp";
constexpr const char* kBackupPath = "/.papyrix/apps/lightsout.bin.bak";
constexpr size_t kRecordSize = 14;

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
  if (std::memcmp(record, "PXLO", 4) || record[4] != 1) return LoadResult::Invalid;
  Game value;
  for (unsigned i = 0; i < 4; ++i) {
    value.lights |= uint32_t(record[5 + i]) << (8 * i);
    value.moves |= uint32_t(record[9 + i]) << (8 * i);
  }
  value.cursor = record[13];
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
  uint8_t record[kRecordSize] = {'P', 'X', 'L', 'O', 1};
  for (unsigned i = 0; i < 4; ++i) {
    record[5 + i] = static_cast<uint8_t>(game.lights >> (8 * i));
    record[9 + i] = static_cast<uint8_t>(game.moves >> (8 * i));
  }
  record[13] = game.cursor;
  return miniapps::publishCheckedFile<FsFile>(
      storage, kPath, kTemporaryPath, kBackupPath, final == LoadResult::Loaded,
      [&](FsFile& file) { return file.write(record, sizeof(record)) == sizeof(record); });
}
}  // namespace papyrix::lightsout
