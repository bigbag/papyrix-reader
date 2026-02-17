#pragma once

#include <cstddef>
#include <cstdint>

namespace papyrix {

// Button identifiers
enum class Button : uint8_t {
  Up,
  Down,
  Left,
  Right,
  Center,
  Back,
  Power,
};

// Content format types
enum class ContentType : uint8_t {
  None = 0,
  Epub,
  Xtc,
  Txt,
  Markdown,
  Fb2,
};

// State identifiers
enum class StateId : uint8_t {
  Startup,
  Home,
  FileList,
  Reader,
  Settings,
  Sync,
  Network,
  CalibreSync,
  Error,
  Sleep,
};

// Sync operation mode
enum class SyncMode : uint8_t {
  None,
  FileTransfer,
  CalibreWireless,
};

// Common buffer sizes
namespace BufferSize {
constexpr size_t Path = 256;
constexpr size_t Text = 512;
constexpr size_t Decompress = 8192;
constexpr size_t Title = 128;
constexpr size_t Author = 64;
constexpr size_t TocTitle = 64;
}  // namespace BufferSize

// Screen dimensions (X4 e-paper)
namespace Screen {
constexpr uint16_t Width = 480;
constexpr uint16_t Height = 800;
constexpr size_t BufferSize = (Width * Height) / 8;  // 1-bit display
}  // namespace Screen

}  // namespace papyrix
