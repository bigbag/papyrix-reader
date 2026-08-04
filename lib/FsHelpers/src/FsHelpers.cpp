#include "FsHelpers.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace {
// Folders/files to hide from file browsers (UI and web interface)
const char* HIDDEN_FS_ITEMS[] = {
    "System Volume Information", "LOST.DIR", "$RECYCLE.BIN", "config", "XTCache", "sleep", "firmware.bin",
    "force_update.bin"};
constexpr size_t HIDDEN_FS_ITEMS_COUNT = sizeof(HIDDEN_FS_ITEMS) / sizeof(HIDDEN_FS_ITEMS[0]);
}  // namespace

int FsHelpers::naturalCompare(const char* a, const char* b) {
  if (!a || !b) {
    if (a == b) return 0;
    return a ? 1 : -1;
  }

  const auto uc = [](char c) { return static_cast<unsigned char>(c); };
  while (*a && *b) {
    if (std::isdigit(uc(*a)) && std::isdigit(uc(*b))) {
      while (*a == '0') a++;
      while (*b == '0') b++;

      size_t lenA = 0;
      size_t lenB = 0;
      while (std::isdigit(uc(a[lenA]))) lenA++;
      while (std::isdigit(uc(b[lenB]))) lenB++;
      if (lenA != lenB) return lenA < lenB ? -1 : 1;

      for (size_t i = 0; i < lenA; i++) {
        if (a[i] != b[i]) return uc(a[i]) < uc(b[i]) ? -1 : 1;
      }
      a += lenA;
      b += lenB;
      continue;
    }

    const int lowerA = std::tolower(uc(*a));
    const int lowerB = std::tolower(uc(*b));
    if (lowerA != lowerB) return lowerA < lowerB ? -1 : 1;
    a++;
    b++;
  }

  if (*a == *b) return 0;
  return *a ? 1 : -1;
}

bool FsHelpers::isHiddenFsItem(const char* name) {
  for (size_t i = 0; i < HIDDEN_FS_ITEMS_COUNT; i++) {
    if (strcmp(name, HIDDEN_FS_ITEMS[i]) == 0) return true;
  }
  return false;
}

std::string FsHelpers::normalisePath(const std::string& path) {
  std::vector<std::string> components;
  std::string component;

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        if (component == "..") {
          if (!components.empty()) {
            components.pop_back();
          }
        } else {
          components.push_back(component);
        }
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    components.push_back(component);
  }

  std::string result;
  for (const auto& c : components) {
    if (!result.empty()) {
      result += "/";
    }
    result += c;
  }

  return result;
}
