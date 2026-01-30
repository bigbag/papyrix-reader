#include "ContentHandle.h"

#include <Epub.h>
#include <Markdown.h>
#include <Txt.h>
#include <Xtc.h>

#include <cstring>  // For memset
#include <new>      // For placement new

namespace papyrix {

// Static empty metadata
ContentMetadata ContentHandle::emptyMetadata_ = {};

// Helper to compute max of variadic sizeof
template <typename T>
constexpr size_t maxSize() {
  return sizeof(T);
}
template <typename T, typename U, typename... Rest>
constexpr size_t maxSize() {
  return sizeof(T) > maxSize<U, Rest...>() ? sizeof(T) : maxSize<U, Rest...>();
}

ContentHandle::ContentHandle() : type(ContentType::None) {
  // Zero-initialize entire union for safety
  constexpr size_t unionSize = maxSize<EpubProvider, XtcProvider, TxtProvider, MarkdownProvider>();
  memset(&epub, 0, unionSize);
}

ContentHandle::~ContentHandle() { close(); }

void ContentHandle::destroyActive() {
  switch (type) {
    case ContentType::Epub:
      epub.~EpubProvider();
      break;
    case ContentType::Xtc:
      xtc.~XtcProvider();
      break;
    case ContentType::Txt:
      txt.~TxtProvider();
      break;
    case ContentType::Markdown:
      markdown.~MarkdownProvider();
      break;
    case ContentType::None:
      break;
  }
  type = ContentType::None;
}

void ContentHandle::constructProvider(ContentType newType) {
  switch (newType) {
    case ContentType::Epub:
      new (&epub) EpubProvider();
      break;
    case ContentType::Xtc:
      new (&xtc) XtcProvider();
      break;
    case ContentType::Txt:
      new (&txt) TxtProvider();
      break;
    case ContentType::Markdown:
      new (&markdown) MarkdownProvider();
      break;
    case ContentType::None:
      break;
  }
  type = newType;
}

Result<void> ContentHandle::open(const char* path, const char* cacheDir) {
  // Close any existing content
  close();

  // Detect format
  ContentType detectedType = detectContentType(path);
  if (detectedType == ContentType::None) {
    return ErrVoid(Error::InvalidFormat);
  }

  // Construct appropriate provider
  constructProvider(detectedType);

  // Open content
  Result<void> result;
  switch (type) {
    case ContentType::Epub:
      result = epub.open(path, cacheDir);
      break;
    case ContentType::Xtc:
      result = xtc.open(path, cacheDir);
      break;
    case ContentType::Txt:
      result = txt.open(path, cacheDir);
      break;
    case ContentType::Markdown:
      result = markdown.open(path, cacheDir);
      break;
    default:
      result = ErrVoid(Error::InvalidFormat);
      break;
  }

  // If open failed, clean up
  if (!result.ok()) {
    destroyActive();
  }

  return result;
}

void ContentHandle::close() { destroyActive(); }

const ContentMetadata& ContentHandle::metadata() const {
  switch (type) {
    case ContentType::Epub:
      return epub.meta;
    case ContentType::Xtc:
      return xtc.meta;
    case ContentType::Txt:
      return txt.meta;
    case ContentType::Markdown:
      return markdown.meta;
    default:
      return emptyMetadata_;
  }
}

uint32_t ContentHandle::pageCount() const {
  switch (type) {
    case ContentType::Epub:
      return epub.pageCount();
    case ContentType::Xtc:
      return xtc.pageCount();
    case ContentType::Txt:
      return txt.pageCount();
    case ContentType::Markdown:
      return markdown.pageCount();
    default:
      return 0;
  }
}

const char* ContentHandle::cacheDir() const {
  const ContentMetadata& m = metadata();
  return (m.cachePath[0] != '\0') ? m.cachePath : nullptr;
}

uint16_t ContentHandle::tocCount() const {
  switch (type) {
    case ContentType::Epub:
      return epub.tocCount();
    case ContentType::Xtc:
      return xtc.tocCount();
    case ContentType::Txt:
      return txt.tocCount();
    case ContentType::Markdown:
      return markdown.tocCount();
    default:
      return 0;
  }
}

Result<TocEntry> ContentHandle::getTocEntry(uint16_t index) const {
  switch (type) {
    case ContentType::Epub:
      return epub.getTocEntry(index);
    case ContentType::Xtc:
      return xtc.getTocEntry(index);
    case ContentType::Txt:
      return txt.getTocEntry(index);
    case ContentType::Markdown:
      return markdown.getTocEntry(index);
    default:
      return Err<TocEntry>(Error::InvalidState);
  }
}

std::string ContentHandle::getThumbnailPath() const {
  switch (type) {
    case ContentType::Epub:
      if (epub.getEpub()) {
        return epub.getEpub()->getThumbBmpPath();
      }
      break;
    case ContentType::Txt:
      if (txt.getTxt()) {
        return txt.getTxt()->getThumbBmpPath();
      }
      break;
    case ContentType::Markdown:
      if (markdown.getMarkdown()) {
        return markdown.getMarkdown()->getThumbBmpPath();
      }
      break;
    default:
      break;
  }
  return "";
}

std::string ContentHandle::getCoverPath() const {
  switch (type) {
    case ContentType::Epub:
      if (epub.getEpub()) {
        return epub.getEpub()->getCoverBmpPath();
      }
      break;
    case ContentType::Txt:
      if (txt.getTxt()) {
        return txt.getTxt()->getCoverBmpPath();
      }
      break;
    case ContentType::Markdown:
      if (markdown.getMarkdown()) {
        return markdown.getMarkdown()->getCoverBmpPath();
      }
      break;
    default:
      break;
  }
  return "";
}

std::string ContentHandle::generateThumbnail() {
  switch (type) {
    case ContentType::Epub:
      if (epub.getEpub() && epub.getEpub()->generateThumbBmp()) {
        return epub.getEpub()->getThumbBmpPath();
      }
      break;
    case ContentType::Txt:
      if (txt.getTxt() && txt.getTxt()->generateThumbBmp()) {
        return txt.getTxt()->getThumbBmpPath();
      }
      break;
    case ContentType::Markdown:
      if (markdown.getMarkdown() && markdown.getMarkdown()->generateThumbBmp()) {
        return markdown.getMarkdown()->getThumbBmpPath();
      }
      break;
    default:
      break;
  }
  return "";
}

std::string ContentHandle::generateCover(bool use1BitDithering) {
  switch (type) {
    case ContentType::Epub:
      if (epub.getEpub() && epub.getEpub()->generateCoverBmp(use1BitDithering)) {
        return epub.getEpub()->getCoverBmpPath();
      }
      break;
    case ContentType::Txt:
      if (txt.getTxt() && txt.getTxt()->generateCoverBmp(use1BitDithering)) {
        return txt.getTxt()->getCoverBmpPath();
      }
      break;
    case ContentType::Markdown:
      if (markdown.getMarkdown() && markdown.getMarkdown()->generateCoverBmp(use1BitDithering)) {
        return markdown.getMarkdown()->getCoverBmpPath();
      }
      break;
    default:
      break;
  }
  return "";
}

// Provider access
EpubProvider* ContentHandle::asEpub() { return type == ContentType::Epub ? &epub : nullptr; }

XtcProvider* ContentHandle::asXtc() { return type == ContentType::Xtc ? &xtc : nullptr; }

TxtProvider* ContentHandle::asTxt() { return type == ContentType::Txt ? &txt : nullptr; }

MarkdownProvider* ContentHandle::asMarkdown() { return type == ContentType::Markdown ? &markdown : nullptr; }

const EpubProvider* ContentHandle::asEpub() const { return type == ContentType::Epub ? &epub : nullptr; }

const XtcProvider* ContentHandle::asXtc() const { return type == ContentType::Xtc ? &xtc : nullptr; }

const TxtProvider* ContentHandle::asTxt() const { return type == ContentType::Txt ? &txt : nullptr; }

const MarkdownProvider* ContentHandle::asMarkdown() const {
  return type == ContentType::Markdown ? &markdown : nullptr;
}

}  // namespace papyrix
