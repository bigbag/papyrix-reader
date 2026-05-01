#include "LiangHyphenation.h"

#include <algorithm>
#include <vector>

namespace {

using EmbeddedAutomaton = SerializedHyphenationPatterns;

// Fixed buffers avoid per-word heap churn that fragments DRAM enough to block
// the 32 KB inflate ring buffer allocation. Sized for German (longest word
// ~63 codepoints * 2 UTF-8 bytes + 2 sentinels). Words exceeding the cap are
// silently skipped (no hyphenation).
static constexpr size_t MAX_WORD_BYTES = 160;
static constexpr size_t MAX_WORD_CHARS = 70;

struct AugmentedWord {
  uint8_t bytes[MAX_WORD_BYTES];
  size_t charByteOffsets[MAX_WORD_CHARS];
  int32_t byteToCharIndex[MAX_WORD_BYTES];
  size_t byteLen = 0;
  size_t charCount_ = 0;

  bool empty() const { return byteLen == 0; }
  size_t charCount() const { return charCount_; }
};

size_t encodeUtf8(uint32_t cp, AugmentedWord& word) {
  if ((cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
    return 0;
  }

  uint8_t encoded[4];
  size_t len = 0;

  if (cp <= 0x7Fu) {
    encoded[len++] = static_cast<uint8_t>(cp);
  } else if (cp <= 0x7FFu) {
    encoded[len++] = static_cast<uint8_t>(0xC0u | ((cp >> 6) & 0x1Fu));
    encoded[len++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
  } else if (cp <= 0xFFFFu) {
    encoded[len++] = static_cast<uint8_t>(0xE0u | ((cp >> 12) & 0x0Fu));
    encoded[len++] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3Fu));
    encoded[len++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
  } else {
    encoded[len++] = static_cast<uint8_t>(0xF0u | ((cp >> 18) & 0x07u));
    encoded[len++] = static_cast<uint8_t>(0x80u | ((cp >> 12) & 0x3Fu));
    encoded[len++] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3Fu));
    encoded[len++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
  }

  if (word.byteLen + len > MAX_WORD_BYTES) {
    return 0;
  }
  for (size_t i = 0; i < len; ++i) {
    word.bytes[word.byteLen++] = encoded[i];
  }
  return len;
}

bool buildAugmentedWord(AugmentedWord& word, const std::vector<CodepointInfo>& cps, const LiangWordConfig& config) {
  word.byteLen = 0;
  word.charCount_ = 0;

  if (cps.empty()) {
    return false;
  }

  word.charByteOffsets[word.charCount_++] = 0;
  word.bytes[word.byteLen++] = '.';

  for (const auto& info : cps) {
    if (!config.isLetter(info.value)) {
      word.byteLen = 0;
      word.charCount_ = 0;
      return false;
    }
    if (word.charCount_ >= MAX_WORD_CHARS - 1) {
      word.byteLen = 0;
      word.charCount_ = 0;
      return false;
    }
    word.charByteOffsets[word.charCount_++] = word.byteLen;
    if (encodeUtf8(config.toLower(info.value), word) == 0) {
      word.byteLen = 0;
      word.charCount_ = 0;
      return false;
    }
  }

  if (word.charCount_ >= MAX_WORD_CHARS || word.byteLen >= MAX_WORD_BYTES) {
    word.byteLen = 0;
    word.charCount_ = 0;
    return false;
  }
  word.charByteOffsets[word.charCount_++] = word.byteLen;
  word.bytes[word.byteLen++] = '.';

  for (size_t i = 0; i < word.byteLen; ++i) {
    word.byteToCharIndex[i] = -1;
  }
  for (size_t i = 0; i < word.charCount_; ++i) {
    const size_t offset = word.charByteOffsets[i];
    if (offset < word.byteLen) {
      word.byteToCharIndex[offset] = static_cast<int32_t>(i);
    }
  }

  return true;
}

struct AutomatonState {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t addr = 0;
  uint8_t stride = 1;
  size_t childCount = 0;
  const uint8_t* transitions = nullptr;
  const uint8_t* targets = nullptr;
  const uint8_t* levels = nullptr;
  size_t levelsLen = 0;

  bool valid() const { return data != nullptr; }
};

AutomatonState decodeState(const EmbeddedAutomaton& automaton, size_t addr) {
  AutomatonState state;
  if (addr >= automaton.size) {
    return state;
  }

  const uint8_t* base = automaton.data + addr;
  size_t remaining = automaton.size - addr;
  size_t pos = 0;

  const uint8_t header = base[pos++];
  const bool hasLevels = (header >> 7) != 0;
  uint8_t stride = static_cast<uint8_t>((header >> 5) & 0x03u);
  if (stride == 0) {
    stride = 1;
  }
  size_t childCount = static_cast<size_t>(header & 0x1Fu);
  if (childCount == 31u) {
    if (pos >= remaining) {
      return AutomatonState{};
    }
    childCount = base[pos++];
  }

  const uint8_t* levelsPtr = nullptr;
  size_t levelsLen = 0;
  if (hasLevels) {
    if (pos + 1 >= remaining) {
      return AutomatonState{};
    }
    const uint8_t offsetHi = base[pos++];
    const uint8_t offsetLoLen = base[pos++];
    const size_t offset = (static_cast<size_t>(offsetHi) << 4) | (offsetLoLen >> 4);
    levelsLen = offsetLoLen & 0x0Fu;
    if (offset < 4u || offset - 4u + levelsLen > automaton.size) {
      return AutomatonState{};
    }
    levelsPtr = automaton.data + offset - 4u;
  }

  if (pos + childCount > remaining) {
    return AutomatonState{};
  }
  const uint8_t* transitions = base + pos;
  pos += childCount;

  const size_t targetsBytes = childCount * stride;
  if (pos + targetsBytes > remaining) {
    return AutomatonState{};
  }
  const uint8_t* targets = base + pos;

  state.data = automaton.data;
  state.size = automaton.size;
  state.addr = addr;
  state.stride = stride;
  state.childCount = childCount;
  state.transitions = transitions;
  state.targets = targets;
  state.levels = levelsPtr;
  state.levelsLen = levelsLen;
  return state;
}

int32_t decodeDelta(const uint8_t* buf, uint8_t stride) {
  if (stride == 1) {
    return static_cast<int8_t>(buf[0]);
  }
  if (stride == 2) {
    return static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]));
  }
  const int32_t unsignedVal =
      (static_cast<int32_t>(buf[0]) << 16) | (static_cast<int32_t>(buf[1]) << 8) | static_cast<int32_t>(buf[2]);
  return unsignedVal - (1 << 23);
}

bool transition(const EmbeddedAutomaton& automaton, const AutomatonState& state, uint8_t letter, AutomatonState& out) {
  if (!state.valid()) {
    return false;
  }

  for (size_t idx = 0; idx < state.childCount; ++idx) {
    if (state.transitions[idx] != letter) {
      continue;
    }
    const uint8_t* deltaPtr = state.targets + idx * state.stride;
    const int32_t delta = decodeDelta(deltaPtr, state.stride);
    const int64_t nextAddr = static_cast<int64_t>(state.addr) + delta;
    if (nextAddr < 0 || static_cast<size_t>(nextAddr) >= automaton.size) {
      return false;
    }
    out = decodeState(automaton, static_cast<size_t>(nextAddr));
    return out.valid();
  }
  return false;
}

std::vector<size_t> collectBreakIndexes(const std::vector<CodepointInfo>& cps, const uint8_t* scores,
                                        const size_t scoresSize, const size_t minPrefix, const size_t minSuffix) {
  std::vector<size_t> indexes;
  const size_t cpCount = cps.size();
  if (cpCount < 2) {
    return indexes;
  }

  for (size_t breakIndex = 1; breakIndex < cpCount; ++breakIndex) {
    if (breakIndex < minPrefix) {
      continue;
    }

    const size_t suffixCount = cpCount - breakIndex;
    if (suffixCount < minSuffix) {
      continue;
    }

    const size_t scoreIdx = breakIndex + 1;
    if (scoreIdx >= scoresSize) {
      break;
    }
    if ((scores[scoreIdx] & 1u) == 0) {
      continue;
    }
    indexes.push_back(breakIndex);
  }

  return indexes;
}

}  // namespace

std::vector<size_t> liangBreakIndexes(const std::vector<CodepointInfo>& cps,
                                      const SerializedHyphenationPatterns& patterns, const LiangWordConfig& config) {
  AugmentedWord augmented;
  if (!buildAugmentedWord(augmented, cps, config)) {
    return {};
  }

  const EmbeddedAutomaton& automaton = patterns;

  const AutomatonState root = decodeState(automaton, automaton.rootOffset);
  if (!root.valid()) {
    return {};
  }

  uint8_t scores[MAX_WORD_CHARS];
  for (size_t i = 0; i < augmented.charCount_; ++i) {
    scores[i] = 0;
  }

  for (size_t charStart = 0; charStart < augmented.charCount_; ++charStart) {
    const size_t byteStart = augmented.charByteOffsets[charStart];
    AutomatonState state = root;

    for (size_t cursor = byteStart; cursor < augmented.byteLen; ++cursor) {
      AutomatonState next;
      if (!transition(automaton, state, augmented.bytes[cursor], next)) {
        break;
      }
      state = next;

      if (state.levels && state.levelsLen > 0) {
        size_t offset = 0;
        for (size_t i = 0; i < state.levelsLen; ++i) {
          const uint8_t packed = state.levels[i];
          const size_t dist = static_cast<size_t>(packed / 10);
          const uint8_t level = static_cast<uint8_t>(packed % 10);

          offset += dist;
          const size_t splitByte = byteStart + offset;
          if (splitByte >= augmented.byteLen) {
            continue;
          }

          const int32_t boundary = augmented.byteToCharIndex[splitByte];
          if (boundary < 0) {
            continue;
          }
          if (boundary < 2 || boundary + 2 > static_cast<int32_t>(augmented.charCount_)) {
            continue;
          }

          const size_t idx = static_cast<size_t>(boundary);
          if (idx >= augmented.charCount_) {
            continue;
          }
          scores[idx] = std::max(scores[idx], level);
        }
      }
    }
  }

  return collectBreakIndexes(cps, scores, augmented.charCount_, config.minPrefix, config.minSuffix);
}
