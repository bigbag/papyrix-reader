#pragma once

#include <Fb2.h>

#include <memory>

#include "../core/Result.h"
#include "ContentTypes.h"

namespace papyrix {

// Fb2Provider wraps the Fb2 handler
struct Fb2Provider {
  std::unique_ptr<Fb2> fb2;
  ContentMetadata meta;

  Fb2Provider() = default;
  ~Fb2Provider() = default;

  // Non-copyable
  Fb2Provider(const Fb2Provider&) = delete;
  Fb2Provider& operator=(const Fb2Provider&) = delete;

  // Movable
  Fb2Provider(Fb2Provider&&) = default;
  Fb2Provider& operator=(Fb2Provider&&) = default;

  Result<void> open(const char* path, const char* cacheDir);
  void close();

  uint32_t pageCount() const;
  uint16_t tocCount() const;
  Result<TocEntry> getTocEntry(uint16_t index) const;

  // Direct access
  Fb2* getFb2() { return fb2.get(); }
  const Fb2* getFb2() const { return fb2.get(); }
};

}  // namespace papyrix
