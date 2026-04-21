#include "ArabicShaper.h"

// Inline UTF-8 decode to avoid header dependency
static uint32_t decodeUtf8(const unsigned char** ptr) {
  if (**ptr == 0) return 0;
  unsigned char c = **ptr;
  uint32_t cp;
  int bytes;
  if (c < 0x80) {
    cp = c;
    bytes = 1;
  } else if ((c >> 5) == 0x6) {
    cp = c & 0x1F;
    bytes = 2;
  } else if ((c >> 4) == 0xE) {
    cp = c & 0x0F;
    bytes = 3;
  } else if ((c >> 3) == 0x1E) {
    cp = c & 0x07;
    bytes = 4;
  } else {
    (*ptr)++;
    return 0xFFFD;
  }
  for (int i = 1; i < bytes; i++) {
    unsigned char cont = (*ptr)[i];
    if ((cont & 0xC0) != 0x80) {
      *ptr += i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  *ptr += bytes;
  return cp;
}

namespace ArabicShaper {

static const ArabicFormEntry* findFormEntry(uint32_t cp) {
  for (size_t i = 0; i < ARABIC_FORMS_COUNT; i++) {
    if (ARABIC_FORMS[i].base == cp) return &ARABIC_FORMS[i];
  }
  return nullptr;
}

uint32_t getContextualForm(uint32_t cp, bool prevJoins, bool nextJoins) {
  const ArabicFormEntry* entry = findFormEntry(cp);
  if (!entry) return cp;

  if (prevJoins && nextJoins && entry->medial != 0) return entry->medial;
  if (prevJoins && entry->final_ != 0) return entry->final_;
  if (nextJoins && entry->initial != 0) return entry->initial;
  if (entry->isolated != 0) return entry->isolated;
  return cp;
}

uint32_t getLamAlefLigature(uint32_t alef, bool prevJoins) {
  for (size_t i = 0; i < LAM_ALEF_COUNT; i++) {
    if (LAM_ALEF_LIGATURES[i].alef == alef) {
      return prevJoins ? LAM_ALEF_LIGATURES[i].final_ : LAM_ALEF_LIGATURES[i].isolated;
    }
  }
  return 0;
}

// Check if a joining type can join to the left (has a connection on its left side)
static bool joinsToLeft(JoiningType jt) { return jt == JoiningType::DUAL_JOINING; }

// Check if a joining type can join to the right (has a connection on its right side)
static bool joinsToRight(JoiningType jt) { return jt == JoiningType::DUAL_JOINING || jt == JoiningType::RIGHT_JOINING; }

// Unicode Bidi_Mirrored property (subset): characters whose glyph must be swapped
// when they end up in a resolved RTL context. Only pairs actually seen in books are
// included to keep the table compact; curly quotes (U+201C..U+201D, U+2018..U+2019)
// are intentionally absent because Unicode marks them Bidi_Mirrored=No.
static uint32_t bidiMirror(uint32_t cp) {
  switch (cp) {
    case 0x0028:
      return 0x0029;  // ( -> )
    case 0x0029:
      return 0x0028;  // ) -> (
    case 0x005B:
      return 0x005D;  // [ -> ]
    case 0x005D:
      return 0x005B;  // ] -> [
    case 0x007B:
      return 0x007D;  // { -> }
    case 0x007D:
      return 0x007B;  // } -> {
    case 0x003C:
      return 0x003E;  // < -> >
    case 0x003E:
      return 0x003C;  // > -> <
    case 0x00AB:
      return 0x00BB;  // « -> »
    case 0x00BB:
      return 0x00AB;  // » -> «
    case 0x2039:
      return 0x203A;  // ‹ -> ›
    case 0x203A:
      return 0x2039;  // › -> ‹
    default:
      return cp;
  }
}

std::vector<uint32_t> shapeText(const char* text) {
  if (!text || !*text) return {};

  // Step 1: Decode UTF-8 to codepoints
  std::vector<uint32_t> codepoints;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
  uint32_t cp;
  while ((cp = decodeUtf8(&ptr))) {
    codepoints.push_back(cp);
  }

  if (codepoints.empty()) return {};

  // Step 2: Apply Lam-Alef ligatures
  std::vector<uint32_t> afterLigatures;
  afterLigatures.reserve(codepoints.size());
  for (size_t i = 0; i < codepoints.size(); i++) {
    if (codepoints[i] == 0x0644 && i + 1 < codepoints.size()) {  // Lam
      // Look ahead past any diacritics to find the Alef
      size_t alefIdx = i + 1;
      while (alefIdx < codepoints.size() && isArabicDiacritic(codepoints[alefIdx])) {
        alefIdx++;
      }
      if (alefIdx < codepoints.size()) {
        // Check if prev char joins to Lam
        bool prevJoins = false;
        for (int p = static_cast<int>(afterLigatures.size()) - 1; p >= 0; p--) {
          JoiningType pjt = getJoiningType(afterLigatures[p]);
          if (pjt == JoiningType::TRANSPARENT) continue;
          prevJoins = joinsToLeft(pjt);
          break;
        }
        uint32_t lig = getLamAlefLigature(codepoints[alefIdx], prevJoins);
        if (lig != 0) {
          afterLigatures.push_back(lig);
          // Copy diacritics between Lam and Alef
          for (size_t d = i + 1; d < alefIdx; d++) {
            afterLigatures.push_back(codepoints[d]);
          }
          i = alefIdx;  // Skip past the Alef
          continue;
        }
      }
    }
    afterLigatures.push_back(codepoints[i]);
  }

  // Step 3: Apply contextual forms
  std::vector<uint32_t> shaped;
  shaped.reserve(afterLigatures.size());

  for (size_t i = 0; i < afterLigatures.size(); i++) {
    uint32_t c = afterLigatures[i];

    // Skip non-Arabic and already-shaped (ligature) codepoints
    if (!isArabicBaseChar(c)) {
      shaped.push_back(c);
      continue;
    }

    // Find previous non-transparent joining type
    bool prevJoins = false;
    for (int p = static_cast<int>(i) - 1; p >= 0; p--) {
      JoiningType pjt = getJoiningType(afterLigatures[p]);
      if (pjt == JoiningType::TRANSPARENT) continue;
      prevJoins = joinsToLeft(pjt);
      break;
    }

    // Find next non-transparent joining type
    bool nextJoins = false;
    for (size_t n = i + 1; n < afterLigatures.size(); n++) {
      JoiningType njt = getJoiningType(afterLigatures[n]);
      if (njt == JoiningType::TRANSPARENT) continue;
      nextJoins = joinsToRight(njt);
      break;
    }

    shaped.push_back(getContextualForm(c, prevJoins, nextJoins));
  }

  // Step 4: Simplified BiDi reordering for visual order
  // Classify each codepoint as RTL, LTR, or NEUTRAL
  enum class BidiDir : uint8_t { LTR, RTL, NEUTRAL };
  const size_t len = shaped.size();
  std::vector<BidiDir> dirs(len);

  for (size_t i = 0; i < len; i++) {
    uint32_t c = shaped[i];
    // Digits (EN + AN) — European, Arabic-Indic, Extended Arabic-Indic (Persian/Urdu).
    // Checked before the Arabic range so AN digits (U+0660-0669) don't get swept into RTL.
    if ((c >= '0' && c <= '9') || (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9)) {
      dirs[i] = BidiDir::LTR;
    } else if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) || (c >= 0xFB50 && c <= 0xFDFF) ||
               (c >= 0xFE70 && c <= 0xFEFF)) {
      dirs[i] = BidiDir::RTL;
    } else if (c <= 0x20 || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '<' ||
               c == '>' || c == ',' || c == '.' || c == ':' || c == ';' || c == '-' || c == '!' || c == '?' ||
               c == '/' || c == '\'' || c == '"' || c == 0x00AB || c == 0x00BB || c == 0x2039 || c == 0x203A) {
      dirs[i] = BidiDir::NEUTRAL;
    } else {
      dirs[i] = BidiDir::LTR;
    }
  }

  // Resolve neutrals: brackets use content direction, others use neighbor context
  for (size_t i = 0; i < len; i++) {
    if (dirs[i] != BidiDir::NEUTRAL) continue;
    uint32_t c = shaped[i];

    if (c == '(' || c == '[') {
      // Opening bracket: look right for first strong char
      BidiDir found = BidiDir::RTL;  // fallback to base direction
      for (size_t j = i + 1; j < len; j++) {
        if (dirs[j] == BidiDir::LTR) {
          found = BidiDir::LTR;
          break;
        }
        if (dirs[j] == BidiDir::RTL) {
          found = BidiDir::RTL;
          break;
        }
      }
      dirs[i] = found;
    } else if (c == ')' || c == ']') {
      // Closing bracket: look left for first strong char
      BidiDir found = BidiDir::RTL;
      for (int j = static_cast<int>(i) - 1; j >= 0; j--) {
        if (dirs[j] == BidiDir::LTR) {
          found = BidiDir::LTR;
          break;
        }
        if (dirs[j] == BidiDir::RTL) {
          found = BidiDir::RTL;
          break;
        }
      }
      dirs[i] = found;
    } else {
      // Other neutrals: if both neighbors agree, use that; else base direction (RTL)
      BidiDir left = BidiDir::RTL, right = BidiDir::RTL;
      for (int j = static_cast<int>(i) - 1; j >= 0; j--) {
        if (dirs[j] != BidiDir::NEUTRAL) {
          left = dirs[j];
          break;
        }
      }
      for (size_t j = i + 1; j < len; j++) {
        if (dirs[j] != BidiDir::NEUTRAL) {
          right = dirs[j];
          break;
        }
      }
      dirs[i] = (left == right) ? left : BidiDir::RTL;
    }
  }

  // Build runs of consecutive same-direction chars
  struct Run {
    size_t start, end;  // [start, end)
    BidiDir dir;
  };
  std::vector<Run> runs;
  if (len > 0) {
    size_t runStart = 0;
    for (size_t i = 1; i <= len; i++) {
      if (i == len || dirs[i] != dirs[runStart]) {
        runs.push_back({runStart, i, dirs[runStart]});
        runStart = i;
      }
    }
  }

  // Build visual order: reverse overall run order (RTL base), reverse chars within RTL runs
  std::vector<uint32_t> visual;
  visual.reserve(len);
  for (int r = static_cast<int>(runs.size()) - 1; r >= 0; r--) {
    if (runs[r].dir == BidiDir::RTL) {
      // RTL run: reverse chars (logical RTL → visual LTR) and apply Bidi_Mirrored
      // to characters like ( ) [ ] « » resolved to RTL context
      for (int i = static_cast<int>(runs[r].end) - 1; i >= static_cast<int>(runs[r].start); i--) {
        visual.push_back(bidiMirror(shaped[i]));
      }
    } else {
      // LTR run: keep char order
      for (size_t i = runs[r].start; i < runs[r].end; i++) {
        visual.push_back(shaped[i]);
      }
    }
  }

  return visual;
}

}  // namespace ArabicShaper
