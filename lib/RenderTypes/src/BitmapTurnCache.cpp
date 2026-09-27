#include "BitmapTurnCache.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <TargetConfig.h>

#include <new>
#include <utility>

#if PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC
struct BitmapTurnCache::Entry {
  std::string path;
  FsFile file;
  Bitmap bitmap;

  explicit Entry(const std::string& path) : path(path), bitmap(file, true) {}
};

BitmapTurnCache::BitmapTurnCache(GfxRenderer& renderer) : renderer_(renderer) { renderer_.setBitmapTurnCache(this); }

BitmapTurnCache::~BitmapTurnCache() { renderer_.setBitmapTurnCache(nullptr); }

Bitmap* BitmapTurnCache::get(const std::string& path) {
  for (const auto& entry : entries_) {
    if (entry->path == path) {
      return entry->bitmap.rewindToData() == BmpReaderError::Ok ? &entry->bitmap : nullptr;
    }
  }
  constexpr size_t kMaxBytes = papyrix::board::kTargetBitmapTurnCacheBytes;
  constexpr size_t kMaxOpenFiles = 4;
  if (entries_.size() >= kMaxOpenFiles || bytes_ >= kMaxBytes) return nullptr;

  std::unique_ptr<Entry> entry(new (std::nothrow) Entry(path));
  if (!entry || !SdMan.openFileForRead("IMB", path, entry->file) ||
      entry->bitmap.parseHeaders() != BmpReaderError::Ok) {
    return nullptr;
  }

  const size_t rowBytes = static_cast<size_t>(entry->bitmap.getRowBytes());
  const size_t height = static_cast<size_t>(entry->bitmap.getHeight());
  if (rowBytes > (kMaxBytes - bytes_) / height || !entry->bitmap.preloadRowsInPsram()) return nullptr;

  bytes_ += rowBytes * height;
  Bitmap* bitmap = &entry->bitmap;
  entries_.push_back(std::move(entry));
  return bitmap->rewindToData() == BmpReaderError::Ok ? bitmap : nullptr;
}
#endif
