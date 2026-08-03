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
  Count,
};

// Content format types
enum class ContentType : uint8_t {
  None = 0,
  Epub,
  Xtc,
  Txt,
  Markdown,
  Fb2,
  Html,
};

// State identifiers
enum class StateId : uint8_t {
  Startup,
  Home,
  FileList,
  Recent,
  Reader,
  Settings,
  Network,
  CalibreSync,
  AppLauncher,
  Error,
  Sleep,
  Count,
};

// Sync operation mode
enum class SyncMode : uint8_t {
  None,
  FileTransfer,
  CalibreWireless,
  WifiSetup,
  NtpSync,
};

// Common buffer sizes
namespace BufferSize {
constexpr size_t Path = 256;
constexpr size_t FilePath = 1024;
constexpr size_t TrashPath = FilePath + (sizeof("/trash") - 1) + (sizeof(" (9999)") - 1);
constexpr size_t Text = 512;
constexpr size_t Decompress = 8192;
constexpr size_t Title = 128;
constexpr size_t Author = 64;
constexpr size_t TocTitle = 64;
}  // namespace BufferSize

}  // namespace papyrix
