#pragma once
#include <EpdFontFamily.h>
#include <SdFat.h>

#include <memory>
#include <string>
#include <vector>

#include "Block.h"

// represents a block of words in the html document
class TextBlock final : public Block {
 public:
  enum BLOCK_STYLE : uint8_t {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
  };

  struct WordData {
    std::string word;
    uint16_t xPos;
    EpdFontFamily::Style style;
  };

 private:
  std::vector<WordData> wordData;
  BLOCK_STYLE style;
  // Occurrence index range of the source words in this line; split pieces
  // share the index of the word they came from. Not serialized.
  uint32_t sourceFirst_ = 0;
  uint32_t sourceLast_ = 0;

 public:
  explicit TextBlock(std::vector<WordData> data, const BLOCK_STYLE style, const uint32_t sourceFirst = 0,
                     const uint32_t sourceLast = 0)
      : wordData(std::move(data)), style(style), sourceFirst_(sourceFirst), sourceLast_(sourceLast) {}
  ~TextBlock() override = default;
  void setStyle(const BLOCK_STYLE style) { this->style = style; }
  BLOCK_STYLE getStyle() const { return style; }
  uint32_t sourceFirst() const { return sourceFirst_; }
  uint32_t sourceLast() const { return sourceLast_; }

  bool isEmpty() override { return wordData.empty(); }
  void layout(GfxRenderer& renderer) override {};
  // given a renderer works out where to break the words into lines
  void render(const GfxRenderer& renderer, int fontId, int x, int y, bool black = true) const;
  BlockType getType() override { return TEXT_BLOCK; }
  const std::vector<WordData>& getWords() const { return wordData; }
  bool serialize(FsFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(FsFile& file);
};
