#include "RecentBooksStore.h"

#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include <cstdio>

#define TAG "RECENT"

namespace papyrix {

namespace {
constexpr char RECENT_FILE[] = "/.papyrix/recent.bin";
constexpr char RECENT_TMP[] = "/.papyrix/recent.bin.tmp";
constexpr size_t MAX_FILE_SIZE = 65536;
}  // namespace

RecentBooksStore& RecentBooksStore::instance() {
  static RecentBooksStore inst;
  return inst;
}

void RecentBooksStore::add(const std::string& path, const std::string& title, const std::string& author) {
  if (!loaded_) {
    load();
  }
  pruneMissing();
  books_ = addToList(std::move(books_), path, title, author, MAX_STORED);
  save();
}

void RecentBooksStore::remove(const std::string& path) {
  if (!loaded_) {
    load();
  }
  books_ = removeFromList(std::move(books_), path);
  save();
}

bool RecentBooksStore::clearAndSave() {
  if (!loaded_) load();
  books_.clear();
  return save();
}

size_t RecentBooksStore::pruneMissing() {
  size_t before = books_.size();
  books_.erase(
      std::remove_if(books_.begin(), books_.end(), [](const RecentBook& b) { return !SdMan.exists(b.path.c_str()); }),
      books_.end());
  return before - books_.size();
}

bool RecentBooksStore::load() {
  books_.clear();
  loaded_ = true;

  FsFile file;
  if (!SdMan.openFileForRead("RECENT", RECENT_FILE, file)) {
    return false;  // no file yet — empty list is valid
  }

  uint32_t len = file.size();
  if (len < 2 || len > MAX_FILE_SIZE) {
    LOG_ERR(TAG, "Bad recent file size %u", static_cast<unsigned>(len));
    file.close();
    return false;
  }

  std::vector<uint8_t> data(len);
  int got = file.read(data.data(), len);
  file.close();
  if (got != static_cast<int>(len)) {
    LOG_ERR(TAG, "Short read %d/%u", got, static_cast<unsigned>(len));
    return false;
  }

  if (!deserializeList(data.data(), data.size(), books_)) {
    LOG_ERR(TAG, "Corrupt or unknown recent file; resetting");
    books_.clear();
    return false;
  }

  if (books_.size() > MAX_STORED) {
    books_.resize(MAX_STORED);
    if (!save()) LOG_ERR(TAG, "Failed to persist recent book limit migration");
  }

  LOG_DBG(TAG, "Loaded %u recent book(s)", static_cast<unsigned>(books_.size()));
  return true;
}

bool RecentBooksStore::save() {
  SdMan.mkdir("/.papyrix");

  std::vector<uint8_t> data = serializeList(books_);

  FsFile file;
  if (!SdMan.openFileForWrite("RECENT", RECENT_TMP, file)) {
    LOG_ERR(TAG, "Failed to open %s for write", RECENT_TMP);
    return false;
  }
  if (!data.empty()) {
    file.write(data.data(), data.size());
  }
  file.sync();
  uint32_t written = file.size();
  file.close();

  if (written != static_cast<uint32_t>(data.size())) {
    LOG_ERR(TAG, "Short recent write %u/%u", static_cast<unsigned>(written), static_cast<unsigned>(data.size()));
    SdMan.remove(RECENT_TMP);
    return false;
  }

  if (!SdMan.commitFile(RECENT_TMP, RECENT_FILE)) {
    LOG_ERR(TAG, "Failed to commit %s", RECENT_FILE);
    return false;
  }

  LOG_DBG(TAG, "Saved %u recent book(s)", static_cast<unsigned>(books_.size()));
  return true;
}

}  // namespace papyrix
