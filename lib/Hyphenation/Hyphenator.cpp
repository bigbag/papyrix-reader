#include "Hyphenator.h"

#include <algorithm>
#include <vector>

#include "HyphenationCommon.h"
#include "LanguageHyphenator.h"
#include "LanguageRegistry.h"

const LanguageHyphenator* Hyphenator::cachedHyphenator_ = nullptr;

namespace {

const LanguageHyphenator* hyphenatorForLanguage(const std::string& langTag) {
  if (langTag.empty()) return nullptr;

  std::string primary;
  primary.reserve(langTag.size());
  for (char c : langTag) {
    if (c == '-' || c == '_') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    primary.push_back(c);
  }
  if (primary.empty()) return nullptr;

  return getLanguageHyphenatorForPrimaryTag(primary);
}

size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  return (index < cps.size()) ? cps[index].byteOffset : (cps.empty() ? 0 : cps.back().byteOffset);
}

std::vector<Hyphenator::BreakInfo> buildExplicitBreakInfos(const std::vector<CodepointInfo>& cps) {
  std::vector<Hyphenator::BreakInfo> breaks;

  for (size_t i = 1; i + 1 < cps.size(); ++i) {
    const uint32_t cp = cps[i].value;
    if (!isExplicitHyphen(cp) || !isAlphabetic(cps[i - 1].value) || !isAlphabetic(cps[i + 1].value)) {
      continue;
    }
    breaks.push_back({cps[i + 1].byteOffset, isSoftHyphen(cp)});
  }

  return breaks;
}

}  // namespace

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string& word, const bool includeFallback) {
  if (word.empty()) {
    return {};
  }

  auto cps = collectCodepoints(word);
  trimSurroundingPunctuationAndFootnote(cps);
  const auto* hyphenator = cachedHyphenator_;

  auto explicitBreakInfos = buildExplicitBreakInfos(cps);
  if (!explicitBreakInfos.empty()) {
    if (hyphenator) {
      // Also run language patterns on alphabetic segments between explicit hyphens
      std::vector<Hyphenator::BreakInfo> merged = explicitBreakInfos;
      size_t segStart = 0;
      for (size_t i = 0; i <= cps.size(); ++i) {
        const bool atEnd = (i == cps.size());
        const bool atHyphen = !atEnd && isExplicitHyphen(cps[i].value);
        if (atEnd || atHyphen) {
          if (i > segStart) {
            std::vector<CodepointInfo> seg(cps.begin() + segStart, cps.begin() + i);
            auto segIndexes = hyphenator->breakIndexes(seg);
            for (const size_t idx : segIndexes) {
              const size_t globalIdx = segStart + idx;
              merged.push_back({byteOffsetForIndex(cps, globalIdx), true});
            }
          }
          segStart = i + 1;
        }
      }
      std::sort(merged.begin(), merged.end(),
                [](const BreakInfo& a, const BreakInfo& b) { return a.byteOffset < b.byteOffset; });
      merged.erase(std::unique(merged.begin(), merged.end(),
                               [](const BreakInfo& a, const BreakInfo& b) { return a.byteOffset == b.byteOffset; }),
                   merged.end());
      return merged;
    }
    return explicitBreakInfos;
  }

  std::vector<size_t> indexes;
  if (hyphenator) {
    indexes = hyphenator->breakIndexes(cps);
  }

  if (includeFallback && indexes.empty()) {
    const size_t minPrefix = hyphenator ? hyphenator->minPrefix() : LiangWordConfig::kDefaultMinPrefix;
    const size_t minSuffix = hyphenator ? hyphenator->minSuffix() : LiangWordConfig::kDefaultMinSuffix;
    for (size_t idx = minPrefix; idx + minSuffix <= cps.size(); ++idx) {
      indexes.push_back(idx);
    }
  }

  if (indexes.empty()) {
    return {};
  }

  std::vector<Hyphenator::BreakInfo> breaks;
  breaks.reserve(indexes.size());
  for (const size_t idx : indexes) {
    breaks.push_back({byteOffsetForIndex(cps, idx), true});
  }

  return breaks;
}

void Hyphenator::setPreferredLanguage(const std::string& lang) { cachedHyphenator_ = hyphenatorForLanguage(lang); }
