#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/Result.h"

class EInkDisplay;

namespace papyrix {
namespace drivers {

class Display {
 public:
  enum class RefreshMode : uint8_t {
    Full,
    Half,
    Fast,
  };

  Result<void> init();
  void shutdown();

  // Buffer access
  uint8_t* getBuffer();
  const uint8_t* getBuffer() const;
  size_t bufferSize() const;

  // Dimensions (runtime — depends on detected device variant)
  uint16_t width() const;
  uint16_t height() const;

  // Rendering control
  void markDirty() { dirty_ = true; }
  bool isDirty() const { return dirty_; }
  void flush(RefreshMode mode = RefreshMode::Fast);
  void clear(uint8_t color = 0xFF);

  // Power management
  void sleep();
  void wake();

  // Access underlying display (for legacy code during migration)
  EInkDisplay& raw();

 private:
  bool dirty_ = false;
  bool initialized_ = false;
};

}  // namespace drivers
}  // namespace papyrix
