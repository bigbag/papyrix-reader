#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace papyrix {

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
};

// RecentBooksStore - persists the most-recently-opened books.
// Pure list/serialization logic is header-only (unit-tested); I/O is in the .cpp.
class RecentBooksStore {
 public:
  static constexpr uint8_t FILE_VERSION = 1;

  // Layout constants (mirror FileListState::getPageItems geometry).
  static constexpr int LIST_START_Y = 60;
  static constexpr int BOTTOM_MARGIN = 70;

  // Pixel pitch for a two-line (title + author) row from the UI font line height.
  // Single source of truth shared by RecentState (display) and ReaderState (store cap).
  static int rowHeight(int lineHeight) { return 2 * lineHeight + 8; }

  // One-screen capacity from display height and row pitch. Scales per device/orientation/font.
  static int maxRecent(int screenHeight, int rowHeight) {
    return std::max(1, (screenHeight - LIST_START_Y - BOTTOM_MARGIN) / std::max(1, rowHeight));
  }

  // ---- Pure logic (header-only, unit-tested) ----

  static std::vector<RecentBook> addToList(std::vector<RecentBook> books, const std::string& path,
                                           const std::string& title, const std::string& author, int maxCount) {
    books.erase(std::remove_if(books.begin(), books.end(), [&](const RecentBook& b) { return b.path == path; }),
                books.end());
    books.insert(books.begin(), {path, title, author});
    return trimList(std::move(books), maxCount);
  }

  static std::vector<RecentBook> removeFromList(std::vector<RecentBook> books, const std::string& path) {
    books.erase(std::remove_if(books.begin(), books.end(), [&](const RecentBook& b) { return b.path == path; }),
                books.end());
    return books;
  }

  static std::vector<RecentBook> trimList(std::vector<RecentBook> books, int maxCount) {
    if (maxCount > 0 && static_cast<int>(books.size()) > maxCount) {
      books.resize(static_cast<size_t>(maxCount));
    }
    return books;
  }

  // Binary layout: [version:u8][count:u8] then count * ([path][title][author]),
  // each string as [len:u32 little-endian][bytes].
  static std::vector<uint8_t> serializeList(const std::vector<RecentBook>& books) {
    std::vector<uint8_t> out;
    out.push_back(FILE_VERSION);
    uint8_t count = static_cast<uint8_t>(std::min<size_t>(books.size(), 255));
    out.push_back(count);
    for (uint8_t i = 0; i < count; i++) {
      appendString(out, books[i].path);
      appendString(out, books[i].title);
      appendString(out, books[i].author);
    }
    return out;
  }

  static bool deserializeList(const uint8_t* data, size_t len, std::vector<RecentBook>& out) {
    out.clear();
    if (!data || len < 2) return false;
    if (data[0] != FILE_VERSION) return false;
    uint8_t count = data[1];
    size_t pos = 2;
    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author;
      if (!readString(data, len, pos, path) || !readString(data, len, pos, title) ||
          !readString(data, len, pos, author)) {
        out.clear();
        return false;
      }
      out.push_back({std::move(path), std::move(title), std::move(author)});
    }
    return true;
  }

  // ---- Singleton instance API (implemented in .cpp; SdMan-backed) ----
  static RecentBooksStore& instance();

  void add(const std::string& path, const std::string& title, const std::string& author, int maxCount);
  void remove(const std::string& path);
  size_t pruneMissing();  // drops entries whose source file no longer exists; returns count removed
  void trimTo(int maxCount);

  bool load();
  bool save();

  const std::vector<RecentBook>& books() const { return books_; }
  void clear() { books_.clear(); }

 private:
  RecentBooksStore() = default;

  static void appendString(std::vector<uint8_t>& out, const std::string& s) {
    uint32_t l = static_cast<uint32_t>(s.size());
    out.push_back(static_cast<uint8_t>(l & 0xFF));
    out.push_back(static_cast<uint8_t>((l >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((l >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((l >> 24) & 0xFF));
    out.insert(out.end(), s.begin(), s.end());
  }

  static bool readString(const uint8_t* data, size_t len, size_t& pos, std::string& out) {
    if (pos + 4 > len) return false;
    uint32_t l = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
                 (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += 4;
    if (l > 65536) return false;  // sanity guard (matches serialization::readString)
    if (pos + l > len) return false;
    out.assign(reinterpret_cast<const char*>(data + pos), l);
    pos += l;
    return true;
  }

  std::vector<RecentBook> books_;
  bool loaded_ = false;
};

}  // namespace papyrix
