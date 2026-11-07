#include "GoStore.h"

#include <CheckedFile.h>
#include <uzlib.h>

#include <algorithm>
#include <cstring>

namespace papyrix::go_store {
namespace {
constexpr const char* kPath = "/.papyrix/apps/go.bin";
constexpr const char* kTemp = "/.papyrix/apps/go.bin.tmp";
constexpr const char* kBackup = "/.papyrix/apps/go.bin.bak";
constexpr uint32_t kHeader = 24;
constexpr uint32_t kRecord = 22;
constexpr uint32_t kMaxTurns = (UINT32_MAX - kHeader) / kRecord;

uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void put32(uint8_t* p, uint32_t n) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}

bool cancelled(const AbortCallback& abort) { return abort && abort(); }

uint32_t hashKey(const gogame::Key& key) {
  uint32_t hash = 2166136261u;
  for (uint8_t b : key) hash = (hash ^ b) * 16777619u;
  return hash;
}

void addKey(uint8_t* filter, const gogame::Key& key) {
  const uint32_t hash = hashKey(key);
  const unsigned bit = hash & 1023;
  filter[bit / 8] |= uint8_t(1u << (bit % 8));
}

struct Scan {
  FsFile* file;
  const uint8_t* filter;
  uint32_t count;
  const AbortCallback* abort;
};

gogame::HistoryResult findKey(void* context, const gogame::Key& key) {
  auto& scan = *static_cast<Scan*>(context);
  if (cancelled(*scan.abort)) return gogame::HistoryResult::Cancelled;
  if (key == gogame::Key{}) return gogame::HistoryResult::Present;
  const unsigned bit = hashKey(key) & 1023;
  if (!(scan.filter[bit / 8] & (1u << (bit % 8)))) return gogame::HistoryResult::Absent;
  uint8_t record[kRecord];
  if (!scan.file->seekSet(kHeader)) return gogame::HistoryResult::IOError;
  for (uint32_t i = 0; i < scan.count; ++i) {
    if (cancelled(*scan.abort)) return gogame::HistoryResult::Cancelled;
    if (scan.file->read(record, sizeof(record)) != int(sizeof(record))) return gogame::HistoryResult::IOError;
    if (std::memcmp(record + 1, key.data(), key.size()) == 0) return gogame::HistoryResult::Present;
  }
  return gogame::HistoryResult::Absent;
}

bool samePosition(const gogame::Position& a, const gogame::Position& b) {
  return a.cells == b.cells && a.toMove == b.toMove && a.passes == b.passes && a.lastMove == b.lastMove &&
         a.outcome == b.outcome && a.turns == b.turns;
}

LoadResult readFile(hal::Storage& storage, const char* path, Game& result, const AbortCallback& abort,
                    uint8_t* outputFilter = nullptr) {
  if (cancelled(abort)) return LoadResult::Cancelled;
  const auto exists = storage.exists(path);
  if (!exists.ok()) return LoadResult::IOError;
  if (!exists.value) return LoadResult::Missing;
  FsFile file;
  if (!storage.openRead(path, file).ok()) return LoadResult::IOError;
  auto finish = [&](LoadResult status) { return file.close() ? status : LoadResult::IOError; };
  if (file.fileSize() < kHeader) return finish(LoadResult::Invalid);
  uint8_t header[kHeader];
  if (cancelled(abort)) return finish(LoadResult::Cancelled);
  if (file.read(header, sizeof(header)) != int(sizeof(header))) return finish(LoadResult::IOError);
  if (std::memcmp(header, "PXGO", 4) || header[4] != 1 || header[5] != 9 || header[6] != 1 ||
      header[7] != gogame::kKomiHalf || header[16] > uint8_t(gogame::Outcome::WhiteResigned) || header[17] ||
      header[18] || header[19])
    return finish(LoadResult::Invalid);
  const uint32_t count = get32(header + 12);
  if (count > kMaxTurns || file.fileSize() != kHeader + count * kRecord) return finish(LoadResult::Invalid);
  Game game;
  game.seed = get32(header + 8);
  uint8_t filter[128] = {};
  addKey(filter, gogame::Key{});
  Scan scan{&file, filter, 0, &abort};
  gogame::History history{&scan, findKey};
  uint32_t crc = uzlib_crc32(header, 20, UINT32_MAX);
  uint8_t record[kRecord];
  for (uint32_t i = 0; i < count; ++i) {
    if (cancelled(abort)) return finish(LoadResult::Cancelled);
    if (!file.seekSet(kHeader + i * kRecord) || file.read(record, sizeof(record)) != int(sizeof(record)))
      return finish(LoadResult::IOError);
    crc = uzlib_crc32(record, sizeof(record), crc);
    gogame::Position next;
    const auto played = gogame::play(game.position, record[0], next, history);
    if (played == gogame::MoveResult::Cancelled) return finish(LoadResult::Cancelled);
    if (played == gogame::MoveResult::IOError) return finish(LoadResult::IOError);
    if (played != gogame::MoveResult::Ok) return finish(LoadResult::Invalid);
    const auto coloring = gogame::key(next);
    if (std::memcmp(record + 1, coloring.data(), coloring.size())) return finish(LoadResult::Invalid);
    game.position = next;
    addKey(filter, coloring);
    ++scan.count;
  }
  if (~crc != get32(header + 20)) return finish(LoadResult::Invalid);
  const auto outcome = static_cast<gogame::Outcome>(header[16]);
  if (outcome == gogame::Outcome::BlackResigned || outcome == gogame::Outcome::WhiteResigned) {
    const auto color = outcome == gogame::Outcome::BlackResigned ? gogame::Color::Black : gogame::Color::White;
    if (!gogame::resign(game.position, color)) return finish(LoadResult::Invalid);
  }
  if (game.position.outcome != outcome) return finish(LoadResult::Invalid);
  const auto status = finish(LoadResult::Loaded);
  if (status == LoadResult::Loaded) {
    if (outputFilter) std::memcpy(outputFilter, filter, sizeof(filter));
    result = game;
  }
  return status;
}

LoadResult chooseFile(hal::Storage& storage, Game& game, const AbortCallback& abort, const char*& path,
                      uint8_t* filter = nullptr) {
  path = kPath;
  auto first = readFile(storage, kPath, game, abort, filter);
  if (first == LoadResult::Loaded || first == LoadResult::IOError || first == LoadResult::Cancelled) return first;
  path = kBackup;
  const auto second = readFile(storage, kBackup, game, abort, filter);
  if (second == LoadResult::Missing && first == LoadResult::Invalid) return first;
  return second;
}

bool removePresent(hal::Storage& storage, const char* path) {
  const auto exists = storage.exists(path);
  return exists.ok() && (!exists.value || storage.remove(path).ok());
}

bool prepare(hal::Storage& storage, bool reset, bool& hasFinal, const AbortCallback& abort) {
  Game ignored;
  auto primary = readFile(storage, kPath, ignored, abort);
  if (primary == LoadResult::IOError || primary == LoadResult::Cancelled) return false;
  auto backup = readFile(storage, kBackup, ignored, abort);
  if (backup == LoadResult::IOError || backup == LoadResult::Cancelled) return false;
  if (primary != LoadResult::Loaded && backup == LoadResult::Loaded) {
    if (!removePresent(storage, kPath) || !storage.rename(kBackup, kPath).ok()) return false;
    primary = LoadResult::Loaded;
    backup = LoadResult::Missing;
  }
  if (!reset && primary != LoadResult::Loaded) return false;
  if (reset && primary == LoadResult::Missing && backup == LoadResult::Invalid) {
    if (!storage.rename(kBackup, kPath).ok()) return false;
    primary = LoadResult::Invalid;
    backup = LoadResult::Missing;
  }
  if (backup != LoadResult::Missing && !removePresent(storage, kBackup)) return false;
  hasFinal = primary != LoadResult::Missing;
  return true;
}

bool publish(hal::Storage& storage, const Game& next, bool reset, bool append, gogame::Move move,
             const AbortCallback& abort) {
  const auto dir = storage.exists("/.papyrix/apps");
  if (!dir.ok() || (!dir.value && !storage.mkdir("/.papyrix/apps").ok())) return false;
  bool hasFinal = false;
  if (!prepare(storage, reset, hasFinal, abort)) return false;
  FsFile source;
  const uint32_t previous = reset ? 0 : next.position.turns - (append ? 1 : 0);
  if (!reset && (!storage.openRead(kPath, source).ok() || !source.seekSet(kHeader))) {
    source.close();
    return false;
  }
  uint8_t header[kHeader] = {'P', 'X', 'G', 'O', 1, 9, 1, uint8_t(gogame::kKomiHalf)};
  put32(header + 8, next.seed);
  put32(header + 12, next.position.turns);
  header[16] = static_cast<uint8_t>(next.position.outcome);
  bool sourceClosed = reset;
  const bool published =
      miniapps::publishCheckedFile<FsFile>(storage, kPath, kTemp, kBackup, hasFinal, [&](FsFile& destination) {
        uint32_t crc = uzlib_crc32(header, 20, UINT32_MAX);
        bool ok = !cancelled(abort) && destination.write(header, sizeof(header)) == sizeof(header);
        uint8_t buffer[512];
        uint32_t left = previous * kRecord;
        while (ok && left) {
          const size_t n = std::min<size_t>(left, sizeof(buffer));
          if (cancelled(abort) || source.read(buffer, n) != int(n) || destination.write(buffer, n) != n) {
            ok = false;
            break;
          }
          crc = uzlib_crc32(buffer, n, crc);
          left -= n;
        }
        if (!reset) {
          sourceClosed = true;
          if (!source.close()) ok = false;
        }
        if (ok && append) {
          uint8_t record[kRecord];
          record[0] = move;
          const auto coloring = gogame::key(next.position);
          std::memcpy(record + 1, coloring.data(), coloring.size());
          ok = !cancelled(abort) && destination.write(record, sizeof(record)) == sizeof(record);
          crc = uzlib_crc32(record, sizeof(record), crc);
        }
        put32(header + 20, ~crc);
        return ok && !cancelled(abort) && destination.seekSet(20) && destination.write(header + 20, 4) == 4;
      });
  if (!sourceClosed) source.close();
  return published;
}
}  // namespace

HistoryReader::~HistoryReader() { close(); }

bool HistoryReader::open(hal::Storage& storage, const Game& game, const AbortCallback& abort) {
  if (!close()) {
    status_ = LoadResult::IOError;
    return false;
  }
  Game saved;
  const char* path = nullptr;
  status_ = chooseFile(storage, saved, abort, path, filter_);
  if (status_ != LoadResult::Loaded) return false;
  if (saved.seed != game.seed || !samePosition(saved.position, game.position)) {
    status_ = LoadResult::Invalid;
    return false;
  }
  if (cancelled(abort)) {
    status_ = LoadResult::Cancelled;
    return false;
  }
  if (!storage.openRead(path, file_).ok()) {
    status_ = LoadResult::IOError;
    return false;
  }
  count_ = saved.position.turns;
  abort_ = abort;
  return true;
}

gogame::HistoryResult HistoryReader::contains(void* context, const gogame::Key& key) {
  auto& reader = *static_cast<HistoryReader*>(context);
  if (!reader.file_) return gogame::HistoryResult::IOError;
  Scan scan{&reader.file_, reader.filter_, reader.count_, &reader.abort_};
  return findKey(&scan, key);
}

gogame::History HistoryReader::history() { return {this, contains}; }

bool HistoryReader::close() {
  const bool ok = !file_ || file_.close();
  count_ = 0;
  abort_ = {};
  return ok;
}

LoadResult load(hal::Storage& storage, Game& game, const AbortCallback& abort) {
  Game saved;
  const char* path = nullptr;
  const auto result = chooseFile(storage, saved, abort, path);
  if (result == LoadResult::Loaded) game = saved;
  return result;
}

bool startNew(hal::Storage& storage, Game& game, uint32_t seed) {
  Game next;
  next.seed = seed;
  if (!publish(storage, next, true, false, gogame::kNoMove, {})) return false;
  game = next;
  return true;
}

gogame::MoveResult play(hal::Storage& storage, Game& game, gogame::Move move, const AbortCallback& abort) {
  bool wasCancelled = false;
  const AbortCallback checkedAbort = [&]() {
    if (cancelled(abort)) wasCancelled = true;
    return wasCancelled;
  };
  HistoryReader reader;
  if (!reader.open(storage, game, checkedAbort))
    return reader.status_ == LoadResult::Cancelled ? gogame::MoveResult::Cancelled : gogame::MoveResult::IOError;
  Game next = game;
  const auto result = gogame::play(game.position, move, next.position, reader.history());
  if (!reader.close()) return gogame::MoveResult::IOError;
  if (result != gogame::MoveResult::Ok) return result;
  if (game.position.turns >= kMaxTurns) return gogame::MoveResult::IOError;
  if (!publish(storage, next, false, true, move, checkedAbort))
    return wasCancelled ? gogame::MoveResult::Cancelled : gogame::MoveResult::IOError;
  game = next;
  return gogame::MoveResult::Ok;
}

bool resign(hal::Storage& storage, Game& game, gogame::Color color) {
  HistoryReader reader;
  if (!reader.open(storage, game) || !reader.close()) return false;
  Game next = game;
  if (!gogame::resign(next.position, color) || !publish(storage, next, false, false, gogame::kNoMove, {})) return false;
  game = next;
  return true;
}

}  // namespace papyrix::go_store
