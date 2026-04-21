#pragma once

#include <cstdint>

/**
 * Script Detection Utility
 *
 * Provides fast detection of script types for text rendering decisions.
 * Used to determine spacing rules and rendering paths for multi-script text.
 */

namespace ScriptDetector {

// Script classification for rendering decisions
enum class Script : uint8_t {
  LATIN,   // Latin, Cyrillic, Greek, and other space-separated scripts
  CJK,     // Chinese, Japanese, Korean (no spaces between characters)
  THAI,    // Thai script (requires shaping, no word spaces)
  ARABIC,  // Arabic script (requires shaping, RTL)
  OTHER    // Symbols, digits, punctuation, unknown
};

/**
 * Classify a word's primary script based on first significant codepoint.
 * For mixed content, returns the script of the first non-ASCII character.
 *
 * @param word UTF-8 encoded word
 * @return Detected script type
 */
Script classify(const char* word);

/**
 * Check if a codepoint is a CJK ideograph (allows line break before/after).
 * Based on UAX #14 Line Break Class ID.
 *
 * Ranges covered:
 * - CJK Unified Ideographs: U+4E00-U+9FFF
 * - CJK Extension A: U+3400-U+4DBF
 * - CJK Compatibility Ideographs: U+F900-U+FAFF
 * - Hiragana: U+3040-U+309F
 * - Katakana: U+30A0-U+30FF
 * - Hangul Syllables: U+AC00-U+D7AF
 * - CJK Extension B+: U+20000-U+2A6DF
 * - Fullwidth forms: U+FF00-U+FFEF
 */
bool isCjkCodepoint(uint32_t cp);

/**
 * Check if a codepoint is in the Thai Unicode block (U+0E00-U+0E7F).
 */
inline bool isThaiCodepoint(uint32_t cp) { return cp >= 0x0E00 && cp <= 0x0E7F; }

/**
 * Check if a codepoint is in an Arabic Unicode block.
 */
inline bool isArabicCodepoint(uint32_t cp) {
  return (cp >= 0x0600 && cp <= 0x06FF) ||  // Arabic
         (cp >= 0x0750 && cp <= 0x077F) ||  // Arabic Supplement
         (cp >= 0xFB50 && cp <= 0xFDFF) ||  // Arabic Presentation Forms-A
         (cp >= 0xFE70 && cp <= 0xFEFE);    // Arabic Presentation Forms-B (U+FEFF is BOM/ZWNBSP, not Arabic)
}

/**
 * Check if text contains any Thai codepoints (for fast-path detection).
 */
bool containsThai(const char* text);

/**
 * Check if text contains any Arabic codepoints (for fast-path detection).
 */
bool containsArabic(const char* text);

/**
 * Check if text contains any CJK codepoints (for fast-path detection).
 */
bool containsCjk(const char* text);

}  // namespace ScriptDetector
