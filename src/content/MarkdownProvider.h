#pragma once

#include <Markdown.h>

#include <memory>

#include "../core/Result.h"
#include "ContentTypes.h"

namespace papyrix {

// MarkdownProvider wraps the Markdown handler
struct MarkdownProvider {
  std::unique_ptr<Markdown> markdown;
  ContentMetadata meta;

  MarkdownProvider() = default;
  ~MarkdownProvider() = default;

  // Non-copyable
  MarkdownProvider(const MarkdownProvider&) = delete;
  MarkdownProvider& operator=(const MarkdownProvider&) = delete;

  // Movable
  MarkdownProvider(MarkdownProvider&&) = default;
  MarkdownProvider& operator=(MarkdownProvider&&) = default;

  Result<void> open(const char* path, const char* cacheDir);
  void close();

  uint32_t pageCount() const;
  uint16_t tocCount() const { return 0; }  // Markdown has no TOC
  Result<TocEntry> getTocEntry(uint16_t index) const { return Err<TocEntry>(Error::InvalidState); }

  // Direct access
  Markdown* getMarkdown() { return markdown.get(); }
  const Markdown* getMarkdown() const { return markdown.get(); }
};

}  // namespace papyrix
