#include <BoardProfiles.h>
#include <StoragePolicy.h>
#include <TargetConfig.h>

#include "test_utils.h"

using papyrix::board::StorageTransport;
using papyrix::sd::makeStorageMountPlan;

int main() {
  TestUtils::TestRunner runner("Storage transport policy");

#if PAPYRIX_TARGET_XTEINK_C3
  const auto* x3 = papyrix::board::findProfile(papyrix::board::BoardId::X3);
  runner.expectTrue(x3 != nullptr, "C3 artifact contains the X3 profile");
  const auto spiPlan = makeStorageMountPlan(x3->storage);
  runner.expectTrue(spiPlan.transport == StorageTransport::Spi, "X3 selects SPI storage");
  runner.expectTrue(spiPlan.controlsPowerPin && spiPlan.activeLevel && !spiPlan.inactiveLevel,
                    "X3 SPI storage controls its active-high rail");

#elif PAPYRIX_TARGET_STICKY
  const auto plan = makeStorageMountPlan(papyrix::board::bootProfile().storage);
  runner.expectTrue(plan.transport == StorageTransport::Spi && plan.controlsPowerPin &&
                        plan.activeLevel && !plan.inactiveLevel, "Sticky uses powered SPI storage");
#else
  const auto* x4pro = &papyrix::board::bootProfile();
  const auto x4proPlan = makeStorageMountPlan(x4pro->storage);
  runner.expectTrue(x4proPlan.transport == StorageTransport::Sdmmc1Bit, "X4 Pro selects 1-bit SDMMC");
  runner.expectTrue(x4proPlan.controlsPowerPin, "X4 Pro controls its SD rail");
  runner.expectTrue(x4proPlan.inactiveLevel, "X4 Pro disables its active-low rail with HIGH");
  runner.expectFalse(x4proPlan.activeLevel, "X4 Pro enables its active-low rail with LOW");
#endif

  return runner.allPassed() ? 0 : 1;
}
