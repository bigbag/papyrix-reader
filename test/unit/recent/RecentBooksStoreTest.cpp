#include "RecentBooksStore.h"

#include "test_utils.h"

#include <string>
#include <vector>

using papyrix::RecentBook;
using papyrix::RecentBooksStore;

static std::vector<RecentBook> makeBooks(std::vector<std::string> paths) {
  std::vector<RecentBook> b;
  for (auto& p : paths) b.push_back({p, p, ""});
  return b;
}

int main() {
  TestUtils::TestRunner runner("RecentBooksStore");

  // addToList: inserts at front
  {
    auto books = makeBooks({"/a", "/b"});
    auto r = RecentBooksStore::addToList(books, "/c", "C", "", 10);
    runner.expectEq(size_t(3), r.size(), "addToList inserts at front: size");
    runner.expectEq(std::string("/c"), r[0].path, "addToList inserts at front: new is first");
    runner.expectEq(std::string("C"), r[0].title, "addToList stores title");
  }

  // addToList: existing path is deduped and moved to front
  {
    auto books = makeBooks({"/a", "/b", "/c"});
    auto r = RecentBooksStore::addToList(books, "/a", "A2", "", 10);
    runner.expectEq(size_t(3), r.size(), "addToList dedup: no duplicate");
    runner.expectEq(std::string("/a"), r[0].path, "addToList dedup: moved to front");
    runner.expectEq(std::string("A2"), r[0].title, "addToList dedup: title refreshed");
  }

  // addToList: trims to maxCount (oldest evicted from the tail)
  {
    auto books = makeBooks({"/a", "/b"});
    auto r = RecentBooksStore::addToList(books, "/c", "C", "", 2);
    runner.expectEq(size_t(2), r.size(), "addToList trims to maxCount: size");
    runner.expectEq(std::string("/c"), r[0].path, "addToList trims: newest kept");
    runner.expectEq(std::string("/a"), r[1].path, "addToList trims: keeps second, oldest (/b) evicted");
  }

  // removeFromList: removes only the matching path
  {
    auto books = makeBooks({"/a", "/b", "/c"});
    auto r = RecentBooksStore::removeFromList(books, "/b");
    runner.expectEq(size_t(2), r.size(), "removeFromList: size");
    runner.expectEq(std::string("/a"), r[0].path, "removeFromList: keeps first");
    runner.expectEq(std::string("/c"), r[1].path, "removeFromList: keeps last");
  }

  // trimList: trims to maxCount
  {
    auto books = makeBooks({"/a", "/b", "/c", "/d"});
    runner.expectEq(size_t(2), RecentBooksStore::trimList(books, 2).size(), "trimList: trims");
    runner.expectEq(size_t(4), RecentBooksStore::trimList(books, 10).size(), "trimList: no-op when under cap");
  }

  // serialize / deserialize round-trip preserves order and fields
  {
    std::vector<RecentBook> books = {{"/p1", "Title One", "Author A"},
                                     {"/p2", "Title Two", ""}};
    auto bytes = RecentBooksStore::serializeList(books);
    std::vector<RecentBook> out;
    bool ok = RecentBooksStore::deserializeList(bytes.data(), bytes.size(), out);
    runner.expectTrue(ok, "round-trip: deserialize ok");
    runner.expectEq(size_t(2), out.size(), "round-trip: count");
    runner.expectEq(std::string("/p1"), out[0].path, "round-trip: path[0]");
    runner.expectEq(std::string("Title One"), out[0].title, "round-trip: title[0]");
    runner.expectEq(std::string("Author A"), out[0].author, "round-trip: author[0]");
    runner.expectEq(std::string(""), out[1].author, "round-trip: empty author preserved");
  }

  // deserialize: corrupt / truncated -> false, out cleared
  {
    uint8_t bad[] = {1, 5, 0, 0};  // version 1, claims 5 entries, truncated
    std::vector<RecentBook> out = {{"/x", "X", ""}};
    bool ok = RecentBooksStore::deserializeList(bad, sizeof(bad), out);
    runner.expectFalse(ok, "corrupt: returns false");
    runner.expectTrue(out.empty(), "corrupt: out cleared");
  }

  // deserialize: unknown version -> false
  {
    uint8_t bad[] = {9, 0};
    std::vector<RecentBook> out;
    runner.expectFalse(RecentBooksStore::deserializeList(bad, sizeof(bad), out), "unknown version: false");
  }

  // deserialize: empty list (version + count 0)
  {
    uint8_t empty[] = {1, 0};
    std::vector<RecentBook> out = {{"/x", "X", ""}};
    runner.expectTrue(RecentBooksStore::deserializeList(empty, sizeof(empty), out), "empty list: ok");
    runner.expectTrue(out.empty(), "empty list: out empty");
  }

  // rowHeight: two UI lines + padding
  {
    runner.expectEq(48, RecentBooksStore::rowHeight(20), "rowHeight(20)=48");
    runner.expectEq(40, RecentBooksStore::rowHeight(16), "rowHeight(16)=40");
  }

  // maxRecent: one-screen capacity from screen height and row pitch
  {
    runner.expectEq(13, RecentBooksStore::maxRecent(800, 48), "maxRecent(800,48)=13");  // (800-130)/48
    runner.expectEq(11, RecentBooksStore::maxRecent(700, 48), "maxRecent(700,48)=11");  // (700-130)/48
    runner.expectEq(1, RecentBooksStore::maxRecent(100, 48), "maxRecent(100,48)=1 (floor at 1)");
    runner.expectEq(16, RecentBooksStore::maxRecent(800, 40), "maxRecent(800,40)=16");  // (800-130)/40
  }

  return runner.allPassed() ? 0 : 1;
}
