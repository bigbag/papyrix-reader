#include "FsHelpers.h"

#include <cstring>
#include <vector>

namespace {
// Folders/files to hide from file browsers (UI and web interface)
const char* HIDDEN_FS_ITEMS[] = {"System Volume Information", "LOST.DIR", "$RECYCLE.BIN", "config", "XTCache"};
constexpr size_t HIDDEN_FS_ITEMS_COUNT = sizeof(HIDDEN_FS_ITEMS) / sizeof(HIDDEN_FS_ITEMS[0]);
}  // namespace

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
