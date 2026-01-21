/**
 * MarkdownSection.h
 *
 * Markdown section handler for page caching
 * Simplified version of EPUB MarkdownSection - Markdown is treated as a single section
 */

#pragma once

#include <SdFat.h>

#include <functional>
#include <memory>
#include <string>

#include "../Markdown.h"
#include "Epub/RenderConfig.h"

class Page;
class GfxRenderer;

class MarkdownSection {
  std::shared_ptr<Markdown> markdown;
  GfxRenderer& renderer;
  std::string filePath;
  FsFile file;

  void writeMarkdownSectionFileHeader(const RenderConfig& config);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  explicit MarkdownSection(const std::shared_ptr<Markdown>& markdown, GfxRenderer& renderer)
      : markdown(markdown), renderer(renderer), filePath(markdown->getCachePath() + "/section.bin") {}
  ~MarkdownSection() = default;

  bool loadMarkdownSectionFile(const RenderConfig& config);
  bool clearCache() const;
  bool createMarkdownSectionFile(const RenderConfig& config, const std::function<void()>& progressSetupFn = nullptr,
                                 const std::function<void(int)>& progressFn = nullptr);
  std::unique_ptr<Page> loadPageFromMarkdownSectionFile();
};
