#include "HyphenationCommon.h"

#include <Utf8.h>

#include <algorithm>

namespace {

uint32_t toLowerLatinImpl(const uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') {
    return cp - 'A' + 'a';
  }
  if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00DE)) {
    return cp + 0x20;
  }

  switch (cp) {
    case 0x0152:      // Œ
      return 0x0153;  // œ
    case 0x0178:      // Ÿ
      return 0x00FF;  // ÿ
    case 0x1E9E:      // ẞ
      return 0x00DF;  // ß
    default:
      return cp;
  }
}

uint32_t toLowerCyrillicImpl(const uint32_t cp) {
  if (cp >= 0x0410 && cp <= 0x042F) {
    return cp + 0x20;
  }
  if (cp == 0x0401) {
    return 0x0451;
  }
  return cp;
}

}  // namespace

uint32_t toLowerLatin(const uint32_t cp) { return toLowerLatinImpl(cp); }

uint32_t toLowerCyrillic(const uint32_t cp) { return toLowerCyrillicImpl(cp); }

bool isLatinLetter(const uint32_t cp) {
  if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
    return true;
  }

  if (((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00F6) || (cp >= 0x00F8 && cp <= 0x00FF)) &&
      cp != 0x00D7 && cp != 0x00F7) {
    return true;
  }

  switch (cp) {
    case 0x0152:  // Œ
    case 0x0153:  // œ
    case 0x0178:  // Ÿ
    case 0x1E9E:  // ẞ
      return true;
    default:
      return false;
  }
}

bool isCyrillicLetter(const uint32_t cp) { return (cp >= 0x0400 && cp <= 0x052F); }

bool isAlphabetic(const uint32_t cp) { return isLatinLetter(cp) || isCyrillicLetter(cp); }

bool isPunctuation(const uint32_t cp) {
  switch (cp) {
    case '-':
    case '.':
    case ',':
    case '!':
    case '?':
    case ';':
    case ':':
    case '"':
    case '\'':
    case ')':
    case '(':
    case 0x00AB:  // «
    case 0x00BB:  // »
    case 0x2018:  // '
    case 0x2019:  // '
    case 0x201C:  // "
    case 0x201D:  // "
    case 0x00A0:  // no-break space
    case '{':
    case '}':
    case '[':
    case ']':
    case '/':
    case 0x203A:  // ›
    case 0x2026:  // …
      return true;
    default:
      return false;
  }
}

bool isAsciiDigit(const uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool isExplicitHyphen(const uint32_t cp) {
  switch (cp) {
    case '-':
    case 0x00AD:  // soft hyphen
    case 0x058A:  // Armenian hyphen
    case 0x2010:  // hyphen
    case 0x2011:  // non-breaking hyphen
    case 0x2012:  // figure dash
    case 0x2013:  // en dash
    case 0x2014:  // em dash
    case 0x2015:  // horizontal bar
    case 0x2043:  // hyphen bullet
    case 0x207B:  // superscript minus
    case 0x208B:  // subscript minus
    case 0x2212:  // minus sign
    case 0x2E17:  // double oblique hyphen
    case 0x2E3A:  // two-em dash
    case 0x2E3B:  // three-em dash
    case 0xFE58:  // small em dash
    case 0xFE63:  // small hyphen-minus
    case 0xFF0D:  // fullwidth hyphen-minus
    case 0x005F:  // Underscore
    case 0x2026:  // Ellipsis
      return true;
    default:
      return false;
  }
}

bool isSoftHyphen(const uint32_t cp) { return cp == 0x00AD; }

void trimSurroundingPunctuationAndFootnote(std::vector<CodepointInfo>& cps) {
  if (cps.empty()) {
    return;
  }

  // Remove trailing footnote references like [12], even if punctuation trails after the closing bracket.
  if (cps.size() >= 3) {
    int end = static_cast<int>(cps.size()) - 1;
    while (end >= 0 && isPunctuation(cps[end].value)) {
      --end;
    }
    int pos = end;
    if (pos >= 0 && isAsciiDigit(cps[pos].value)) {
      while (pos >= 0 && isAsciiDigit(cps[pos].value)) {
        --pos;
      }
      if (pos >= 0 && cps[pos].value == '[' && end - pos > 1) {
        cps.erase(cps.begin() + pos, cps.end());
      }
    }
  }

  {
    auto first = std::find_if(cps.begin(), cps.end(), [](const CodepointInfo& c) { return !isPunctuation(c.value); });
    cps.erase(cps.begin(), first);
  }
  while (!cps.empty() && isPunctuation(cps.back().value)) {
    cps.pop_back();
  }
}

namespace {

uint32_t composeNfc(const uint32_t base, const uint32_t combining) {
  // clang-format off
  switch (combining) {
    case 0x0300:  // combining grave
      switch (base) {
        case 'A': return 0x00C0; case 'E': return 0x00C8; case 'I': return 0x00CC;
        case 'O': return 0x00D2; case 'U': return 0x00D9;
        case 'a': return 0x00E0; case 'e': return 0x00E8; case 'i': return 0x00EC;
        case 'o': return 0x00F2; case 'u': return 0x00F9;
        default: return 0;
      }
    case 0x0301:  // combining acute
      switch (base) {
        case 'A': return 0x00C1; case 'E': return 0x00C9; case 'I': return 0x00CD;
        case 'O': return 0x00D3; case 'U': return 0x00DA; case 'Y': return 0x00DD;
        case 'a': return 0x00E1; case 'e': return 0x00E9; case 'i': return 0x00ED;
        case 'o': return 0x00F3; case 'u': return 0x00FA; case 'y': return 0x00FD;
        default: return 0;
      }
    case 0x0302:  // combining circumflex
      switch (base) {
        case 'A': return 0x00C2; case 'E': return 0x00CA; case 'I': return 0x00CE;
        case 'O': return 0x00D4; case 'U': return 0x00DB;
        case 'a': return 0x00E2; case 'e': return 0x00EA; case 'i': return 0x00EE;
        case 'o': return 0x00F4; case 'u': return 0x00FB;
        default: return 0;
      }
    case 0x0303:  // combining tilde
      switch (base) {
        case 'A': return 0x00C3; case 'N': return 0x00D1; case 'O': return 0x00D5;
        case 'a': return 0x00E3; case 'n': return 0x00F1; case 'o': return 0x00F5;
        default: return 0;
      }
    case 0x0308:  // combining diaeresis
      switch (base) {
        case 'A': return 0x00C4; case 'E': return 0x00CB; case 'I': return 0x00CF;
        case 'O': return 0x00D6; case 'U': return 0x00DC;
        case 'a': return 0x00E4; case 'e': return 0x00EB; case 'i': return 0x00EF;
        case 'o': return 0x00F6; case 'u': return 0x00FC; case 'y': return 0x00FF;
        default: return 0;
      }
    case 0x0327:  // combining cedilla
      switch (base) {
        case 'C': return 0x00C7; case 'c': return 0x00E7;
        default: return 0;
      }
    default:
      return 0;
  }
  // clang-format on
}

}  // namespace

std::vector<CodepointInfo> collectCodepoints(const std::string& word) {
  std::vector<CodepointInfo> cps;
  cps.reserve(word.size());

  const unsigned char* base = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* ptr = base;
  while (*ptr != 0) {
    const unsigned char* current = ptr;
    const uint32_t cp = utf8NextCodepoint(&ptr);

    if (!cps.empty() && cp >= 0x0300 && cp <= 0x036F) {
      const uint32_t composed = composeNfc(cps.back().value, cp);
      if (composed != 0) {
        cps.back().value = composed;
        continue;
      }
    }

    cps.push_back({cp, static_cast<size_t>(current - base)});
  }

  return cps;
}
