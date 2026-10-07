#include "BoardProfiles.h"
#if PAPYRIX_TARGET_X4PRO
#include <Cw2017Policy.h>
#endif

namespace papyrix::board {

namespace {

#if PAPYRIX_TARGET_XTEINK_C3
// The profile uses the UC8253 10 MHz baseline.
// The UC8279 driver selects 20 MHz.
constexpr BoardProfile kX3Profile = {
    BoardId::X3,
    McuFamily::Esp32C3,
    "xteink_x3",
    "/.papyrix/cache/x3",
    "x3",
    {792, 528, 8, 10, 21, 4, 5, 6, kPinUnused, 10000000, false},
    {InputStyle::AdcLadder, 1, 2, kPinUnused, kPinUnused, kPinUnused, kPinUnused, kPinUnused, kPinUnused, 3, false},
    {TouchController::None, kPinUnused, kPinUnused, kPinUnused, kPinUnused, 0, kPinUnused, false, false, false, false,
     false},
    {StorageTransport::Spi, 12, 7, kPinUnused, kPinUnused, kPinUnused, 13, true},
    {BatteryBackend::Bq27220, kPinUnused, 20, 0, 0x55, kPinUnused, false, 400000, nullptr, 650},
    {RtcType::Ds3231, 20, 0, 400000, 0x68},
    {kPinUnused, kPinUnused, 0, 0, false},
    {kPinUnused, true, true},
    {kPinUnused, false},
};
#endif

#if PAPYRIX_TARGET_XTEINK_C3
constexpr BoardProfile kX4Profile = {
    BoardId::X4,
    McuFamily::Esp32C3,
    "xteink_x4",
    "/.papyrix/cache",
    "",
    {800, 480, 8, 10, 21, 4, 5, 6, kPinUnused, 40000000, false},
    {InputStyle::AdcLadder, 1, 2, kPinUnused, kPinUnused, kPinUnused, kPinUnused, kPinUnused, kPinUnused, 3, false},
    {TouchController::None, kPinUnused, kPinUnused, kPinUnused, kPinUnused, 0, kPinUnused, false, false, false, false,
     false},
    {StorageTransport::Spi, 12, 7, kPinUnused, kPinUnused, kPinUnused, kPinUnused, false},
    {BatteryBackend::Adc, 0, kPinUnused, kPinUnused, 0, kPinUnused, false, 0},
    {RtcType::None, kPinUnused, kPinUnused, 0, 0},
    {kPinUnused, kPinUnused, 0, 0, false},
    {20, false, true},
    {13, true},
};
#endif

#if PAPYRIX_TARGET_X4PRO
// Hardware tests must confirm the X4 Pro display rotation and warm-light channel.
// No verified USB detection pin is available.
constexpr BoardProfile kX4ProProfile = {
    BoardId::X4Pro,
    McuFamily::Esp32S3,
    "xteink_x4_pro",
    "/.papyrix/cache/x4pro",
    "x4pro",
    {800, 480, 12, 11, 13, 18, 14, 6, kPinUnused, 20000000, false},
    {InputStyle::DigitalButtons, kPinUnused, kPinUnused, kPinUnused, kPinUnused, kPinUnused, kPinUnused, 0, 7, 3,
     false},
    {TouchController::Gt911, 39, 38, 10, 4, 0x5D, 2, false, true, false, true, true, 0, 799, 0, 479, 100000, 0x14,
     true},
    {StorageTransport::Sdmmc1Bit, kPinUnused, kPinUnused, 41, 42, 40, 5, false},
    {BatteryBackend::Cw2017, kPinUnused, 39, 38, 0x63, 21, true, 100000,
     papyrix::battery::kCw2017BatteryProfile.data()},
    {RtcType::Bm8563, 39, 38, 100000, 0x51},
    {8, 9, 10000, 10, true},
    {kPinUnused, true, true},
    {1, true},
};
#endif

#if PAPYRIX_TARGET_X4CLASSIC
constexpr BoardProfile kX4ClassicProfile = {
    BoardId::X4Classic,
    McuFamily::Esp32S3,
    "xteink_x4_classic",
    "/.papyrix/cache/x4c",
    "x4c",
    {800, 480, 12, 11, 13, 14, 10, 18, kPinUnused, 10000000, false},
    {InputStyle::DigitalButtons, kPinUnused, kPinUnused, 9, 8, 5, 2, 7, 0, 3, false},
    {TouchController::None, kPinUnused, kPinUnused, kPinUnused, kPinUnused, 0, kPinUnused, false, false, false, false,
     false},
    {StorageTransport::Sdmmc1Bit, kPinUnused, kPinUnused, 41, 42, 40, 6, false},
    {BatteryBackend::Cw2017, kPinUnused, 39, 38, 0x63, 21, true, 400000},
    {RtcType::Bm8563, 39, 38, 400000, 0x51},
    {kPinUnused, kPinUnused, 0, 0, false},
    {kPinUnused, true, true},
    {1, true},
};
#endif

}  // namespace

const BoardProfile* findProfile(BoardId id) {
  switch (id) {
#if PAPYRIX_TARGET_XTEINK_C3
    case BoardId::X3:
      return &kX3Profile;
    case BoardId::X4:
      return &kX4Profile;
#endif
#if PAPYRIX_TARGET_X4PRO
    case BoardId::X4Pro:
      return &kX4ProProfile;
#endif
#if PAPYRIX_TARGET_X4CLASSIC
    case BoardId::X4Classic:
      return &kX4ClassicProfile;
#endif
  }
  return nullptr;
}

const BoardProfile& bootProfile() { return *findProfile(kBootBoardId); }

bool targetSupports(BoardId id) { return findProfile(id) != nullptr; }

}  // namespace papyrix::board
