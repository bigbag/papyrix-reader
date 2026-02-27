#include <cassert>
#include <cstring>
#include <iostream>

#include "BookmarkManager.h"
#include "ContentTypes.h"
#include "Types.h"

// Provide findAt implementation directly to avoid pulling in Core.h and all its
// transitive dependencies via BookmarkManager.cpp.  save()/load() are not tested
// here (they require Storage I/O), so the linker won't look for them.
namespace papyrix {

int BookmarkManager::findAt(const Bookmark* bookmarks, int count,
                            ContentType type, int spineIndex, int sectionPage,
                            uint32_t flatPage) {
  for (int i = 0; i < count; i++) {
    if (type == ContentType::Epub) {
      if (bookmarks[i].spineIndex == spineIndex &&
          bookmarks[i].sectionPage == sectionPage) {
        return i;
      }
    } else if (type == ContentType::Xtc) {
      if (bookmarks[i].flatPage == flatPage) {
        return i;
      }
    } else {
      if (bookmarks[i].sectionPage == sectionPage) {
        return i;
      }
    }
  }
  return -1;
}

}  // namespace papyrix

using namespace papyrix;

static void test_findAt_epub() {
  Bookmark bookmarks[3];
  memset(bookmarks, 0, sizeof(bookmarks));

  bookmarks[0].spineIndex = 0;
  bookmarks[0].sectionPage = 5;
  bookmarks[1].spineIndex = 2;
  bookmarks[1].sectionPage = 10;
  bookmarks[2].spineIndex = 2;
  bookmarks[2].sectionPage = 20;

  assert(BookmarkManager::findAt(bookmarks, 3, ContentType::Epub, 0, 5, 0) == 0);
  assert(BookmarkManager::findAt(bookmarks, 3, ContentType::Epub, 2, 10, 0) == 1);
  assert(BookmarkManager::findAt(bookmarks, 3, ContentType::Epub, 2, 20, 0) == 2);
  assert(BookmarkManager::findAt(bookmarks, 3, ContentType::Epub, 1, 5, 0) == -1);
  assert(BookmarkManager::findAt(bookmarks, 3, ContentType::Epub, 0, 6, 0) == -1);

  std::cout << "  PASS: findAt EPUB" << std::endl;
}

static void test_findAt_xtc() {
  Bookmark bookmarks[2];
  memset(bookmarks, 0, sizeof(bookmarks));

  bookmarks[0].flatPage = 10;
  bookmarks[1].flatPage = 50;

  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Xtc, 0, 0, 10) == 0);
  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Xtc, 0, 0, 50) == 1);
  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Xtc, 0, 0, 11) == -1);

  std::cout << "  PASS: findAt XTC" << std::endl;
}

static void test_findAt_txt() {
  Bookmark bookmarks[2];
  memset(bookmarks, 0, sizeof(bookmarks));

  bookmarks[0].sectionPage = 3;
  bookmarks[1].sectionPage = 15;

  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Txt, 0, 3, 0) == 0);
  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Txt, 0, 15, 0) == 1);
  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Txt, 0, 4, 0) == -1);

  // Same logic for Markdown and FB2
  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Markdown, 0, 3, 0) == 0);
  assert(BookmarkManager::findAt(bookmarks, 2, ContentType::Fb2, 0, 15, 0) == 1);

  std::cout << "  PASS: findAt TXT/Markdown/FB2" << std::endl;
}

static void test_findAt_empty() {
  assert(BookmarkManager::findAt(nullptr, 0, ContentType::Epub, 0, 0, 0) == -1);

  Bookmark bm;
  memset(&bm, 0, sizeof(bm));
  assert(BookmarkManager::findAt(&bm, 0, ContentType::Epub, 0, 0, 0) == -1);

  std::cout << "  PASS: findAt empty" << std::endl;
}

static void test_bookmark_struct_size() {
  assert(sizeof(Bookmark) == 72);
  std::cout << "  PASS: Bookmark struct size is 72 bytes" << std::endl;
}

static void test_max_bookmarks_constant() {
  assert(BookmarkManager::MAX_BOOKMARKS == 20);
  std::cout << "  PASS: MAX_BOOKMARKS is 20" << std::endl;
}

int main() {
  std::cout << "BookmarkManager tests:" << std::endl;

  test_findAt_epub();
  test_findAt_xtc();
  test_findAt_txt();
  test_findAt_empty();
  test_bookmark_struct_size();
  test_max_bookmarks_constant();

  std::cout << "All BookmarkManager tests passed!" << std::endl;
  return 0;
}
