#pragma once

#include "../core/Result.h"
#include "ContentTypes.h"
#include "EpubProvider.h"
#include "Fb2Provider.h"
#include "MarkdownProvider.h"
#include "TxtProvider.h"
#include "XtcProvider.h"

namespace papyrix {

// ContentHandle - Tagged union holding one content provider at a time
// Only one provider is active - saves memory by not allocating all providers
struct ContentHandle {
  ContentType type = ContentType::None;

  // Tagged union - only one is valid at a time based on 'type'
  union {
    EpubProvider epub;
    XtcProvider xtc;
    TxtProvider txt;
    MarkdownProvider markdown;
    Fb2Provider fb2;
  };

  ContentHandle();
  ~ContentHandle();

  // Non-copyable (union contains non-trivial types)
  ContentHandle(const ContentHandle&) = delete;
  ContentHandle& operator=(const ContentHandle&) = delete;

  // Open content file (auto-detects format)
  Result<void> open(const char* path, const char* cacheDir);

  // Close current content
  void close();

  // Check if content is open
  bool isOpen() const { return type != ContentType::None; }

  // Metadata access
  const ContentMetadata& metadata() const;

  // Page/section count
  uint32_t pageCount() const;

  // Cache directory (for progress saving)
  const char* cacheDir() const;

  // TOC access
  uint16_t tocCount() const;
  Result<TocEntry> getTocEntry(uint16_t index) const;

  // Cover generation helpers - return empty string on failure
  std::string getThumbnailPath() const;              // Get path without generating (for existence check)
  std::string getCoverPath() const;                  // Get cover.bmp path without generating
  std::string generateThumbnail();                   // For home screen (THUMB_WIDTH x THUMB_HEIGHT 1-bit)
  std::string generateCover(bool use1BitDithering);  // For reader cover page

  // Direct provider access (for format-specific operations)
  EpubProvider* asEpub();
  XtcProvider* asXtc();
  TxtProvider* asTxt();
  MarkdownProvider* asMarkdown();
  Fb2Provider* asFb2();

  const EpubProvider* asEpub() const;
  const XtcProvider* asXtc() const;
  const TxtProvider* asTxt() const;
  const MarkdownProvider* asMarkdown() const;
  const Fb2Provider* asFb2() const;

 private:
  void destroyActive();
  void constructProvider(ContentType newType);

  // Empty metadata for when nothing is open
  static ContentMetadata emptyMetadata_;
};

}  // namespace papyrix
