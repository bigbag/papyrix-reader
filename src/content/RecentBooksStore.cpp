#include "RecentBooksStore.h"

#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <esp_heap_caps.h>

#include <utility>

#define TAG "RECENT"

namespace papyrix {

namespace {
constexpr char RECENT_FILE[] = "/.papyrix/recent.bin";
constexpr char RECENT_TMP[] = "/.papyrix/recent.bin.tmp";
constexpr size_t MAX_FILE_SIZE = 65536;

bool hasReadHeadroom(size_t fileSize, uint8_t count) {
  const size_t recordBytes = count * sizeof(RecentBook);
  const size_t estimatedBytes = 2 * fileSize + recordBytes;
  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  return estimatedBytes <= freeBytes * 80 / 100 && std::max(fileSize, recordBytes) <= largestBlock * 80 / 100;
}

}  // namespace

RecentBooksStore& RecentBooksStore::instance() {
  static RecentBooksStore inst;
  return inst;
}

void RecentBooksStore::add(const std::string& path, const std::string& title, const std::string& author) {
  if (!loaded_ && !load()) return;
  pruneMissing();
  books_ = addToList(std::move(books_), path, title, author, MAX_STORED);
  if (!save()) LOG_ERR(TAG, "Failed to save recent books");
}

void RecentBooksStore::remove(const std::string& path) {
  if (!loaded_ && !load()) return;
  books_ = removeFromList(std::move(books_), path);
  if (!save()) LOG_ERR(TAG, "Failed to save recent books");
}

bool RecentBooksStore::clearAndSave() {
  if (!loaded_ && !load()) return false;
  books_.clear();
  return save();
}

size_t RecentBooksStore::pruneMissing() {
  if (!loaded_) return 0;
  size_t before = books_.size();
  books_.erase(
      std::remove_if(books_.begin(), books_.end(), [](const RecentBook& b) { return !SdMan.exists(b.path.c_str()); }),
      books_.end());
  return before - books_.size();
}

bool RecentBooksStore::load() {
  loaded_ = false;
  if (!SdMan.exists(RECENT_FILE)) {
    books_.clear();
    loaded_ = true;
    return true;
  }

  FsFile file;
  if (!SdMan.openFileForRead(TAG, RECENT_FILE, file)) {
    LOG_ERR(TAG, "Failed to open %s", RECENT_FILE);
    return false;
  }

  const uint32_t len = file.size();
  uint8_t header[2];
  if (len < sizeof(header) || len > MAX_FILE_SIZE ||
      file.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) || header[0] != FILE_VERSION ||
      header[1] > (len - sizeof(header)) / (3 * sizeof(uint32_t)) || !hasReadHeadroom(len, header[1])) {
    LOG_ERR(TAG, "Cannot load recent file (%u bytes)", static_cast<unsigned>(len));
    file.close();
    return false;
  }

  std::vector<uint8_t> data(len);
  std::copy_n(header, sizeof(header), data.data());
  const size_t bodySize = len - sizeof(header);
  const int got = file.read(data.data() + sizeof(header), bodySize);
  file.close();
  if (got != static_cast<int>(bodySize)) {
    LOG_ERR(TAG, "Short read %d/%u", got, static_cast<unsigned>(bodySize));
    return false;
  }

  std::vector<RecentBook> staged;
  staged.reserve(header[1]);
  if (!deserializeList(data.data(), data.size(), staged)) {
    LOG_ERR(TAG, "Corrupt recent file; saved copy kept");
    return false;
  }

  books_ = std::move(staged);
  loaded_ = true;

  if (books_.size() > MAX_STORED) {
    books_.resize(MAX_STORED);
    if (!save()) LOG_ERR(TAG, "Failed to persist recent book limit migration");
  }

  LOG_DBG(TAG, "Loaded %u recent book(s)", static_cast<unsigned>(books_.size()));
  return true;
}

bool RecentBooksStore::save() {
  if (!loaded_) {
    LOG_ERR(TAG, "Save refused before successful load");
    return false;
  }
  SdMan.mkdir("/.papyrix");

  const std::vector<uint8_t> data = serializeList(books_);

  FsFile file;
  if (!SdMan.openFileForWrite(TAG, RECENT_TMP, file)) {
    LOG_ERR(TAG, "Failed to open %s for write", RECENT_TMP);
    return false;
  }
  const bool written = file.write(data.data(), data.size()) == data.size();
  const bool synced = written && file.sync();
  file.close();
  if (!synced) {
    LOG_ERR(TAG, "Failed recent write or sync");
    SdMan.remove(RECENT_TMP);
    return false;
  }

  if (!SdMan.commitFile(RECENT_TMP, RECENT_FILE)) {
    LOG_ERR(TAG, "Failed to commit %s", RECENT_FILE);
    SdMan.remove(RECENT_TMP);
    return false;
  }

  LOG_DBG(TAG, "Saved %u recent book(s)", static_cast<unsigned>(books_.size()));
  return true;
}

}  // namespace papyrix
