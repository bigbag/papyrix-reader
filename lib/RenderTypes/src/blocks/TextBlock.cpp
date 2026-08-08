#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define TAG "TEXT_BLOCK"

IRAM_ATTR void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y,
                                 const bool black) const {
  for (const auto& wd : wordData) {
    renderer.drawText(fontId, wd.xPos + x, y, wd.word.c_str(), black, wd.style);
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (wordData.size() > UINT16_MAX) return false;
  const uint16_t wordCount = static_cast<uint16_t>(wordData.size());
  if (!serialization::writePodChecked(file, wordCount)) return false;

  // Write words, then xpos, then styles (maintains backward-compatible format)
  for (const auto& wd : wordData) {
    if (!serialization::writeStringChecked(file, wd.word)) return false;
  }
  for (const auto& wd : wordData) {
    if (!serialization::writePodChecked(file, wd.xPos)) return false;
  }
  for (const auto& wd : wordData) {
    if (!serialization::writePodChecked(file, wd.style)) return false;
  }

  return serialization::writePodChecked(file, style);
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  BLOCK_STYLE style;

  // Word count
  if (!serialization::readPodChecked(file, wc)) {
    return nullptr;
  }

  // Sanity check: prevent allocation of unreasonably large vectors (max 10000 words per block)
  if (wc > 10000) {
    LOG_ERR(TAG, "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  // Read into temporary arrays (backward-compatible format: words, then xpos, then styles)
  std::vector<std::string> words(wc);
  std::vector<uint16_t> xpos(wc);
  std::vector<EpdFontFamily::Style> styles(wc);

  for (auto& w : words) {
    if (!serialization::readString(file, w)) {
      return nullptr;
    }
  }
  for (auto& x : xpos) {
    if (!serialization::readPodChecked(file, x)) {
      return nullptr;
    }
  }
  for (auto& s : styles) {
    if (!serialization::readPodChecked(file, s)) {
      return nullptr;
    }
  }

  // Block style
  if (!serialization::readPodChecked(file, style)) {
    return nullptr;
  }

  // Combine into WordData vector
  std::vector<WordData> data;
  data.reserve(wc);
  for (uint16_t i = 0; i < wc; ++i) {
    data.push_back({std::move(words[i]), xpos[i], styles[i]});
  }

  return std::unique_ptr<TextBlock>(new TextBlock(std::move(data), style));
}
