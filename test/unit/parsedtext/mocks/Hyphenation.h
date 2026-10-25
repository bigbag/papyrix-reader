#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Use fixed word breaks for interrupted-layout tests.
namespace Hyphenation {

struct BreakInfo {
  size_t byteOffset;
  bool requiresInsertedHyphen;
};

inline std::vector<BreakInfo> breakOffsets(const std::string& word, bool) {
  if (word == "abcdefghijklmnopqrstuvwxyz") return {{9, true}, {18, true}};
  return {};
}
inline void setLanguage(const std::string&) {}

}  // namespace Hyphenation
