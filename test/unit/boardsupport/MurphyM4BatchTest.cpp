#include <MurphyM4Batch.h>
#include <Preferences.h>

#include "test_utils.h"

using namespace papyrix::board;
int main() {
  TestUtils::TestRunner runner("M4 boot-selected hardware batch");
  Preferences store;
  store.begin("papyrix_hw");
  store.remove("m4_batch");
  store.end();
  loadMurphyM4Batch();
  runner.expectTrue(murphyM4Batch() == MurphyM4Batch::Second, "missing selection uses Batch 2");
  runner.expectEq(unsigned{0x50}, unsigned(murphyM4HalfTemperature(murphyM4Batch())), "Batch 2 panel temperature");
  runner.expectEq(uint16_t{40}, mapMurphyM4ShortAxis(0, MurphyM4Batch::Second, 479), "Batch 2 lower calibration");
  runner.expectEq(uint16_t{41}, mapMurphyM4ShortAxis(0, MurphyM4Batch::First, 479), "Batch 1 lower calibration");
  for (auto batch : {MurphyM4Batch::First, MurphyM4Batch::Second}) {
    runner.expectEq(uint16_t{479}, mapMurphyM4ShortAxis(65535, batch, 479), "raw overflow saturates before multiply");
    runner.expectEq(uint16_t{0}, mapMurphyM4ShortAxis(0, batch, 0), "empty output range stays zero");
  }
  const auto calls = Preferences::putCalls;
  runner.expectTrue(saveMurphyM4Batch(MurphyM4Batch::Second) == MurphyM4BatchSaveResult::Unchanged,
                    "same-value confirmation does not save");
  runner.expectEq(calls, Preferences::putCalls, "same-value confirmation does not write NVS");
  for (bool failOpen : {true, false}) {
    Preferences::failBegin = failOpen;
    Preferences::failPut = !failOpen;
    runner.expectTrue(saveMurphyM4Batch(MurphyM4Batch::First) == MurphyM4BatchSaveResult::Failed,
                      "open or write failure is reported");
    runner.expectTrue(murphyM4Batch() == MurphyM4Batch::Second && murphyM4HalfTemperature(murphyM4Batch()) == 0x50,
                      "failure cannot apply half of the live configuration");
    Preferences::failBegin = Preferences::failPut = false;
    loadMurphyM4Batch();
    runner.expectTrue(murphyM4Batch() == MurphyM4Batch::Second, "failed selection does not survive reboot");
  }
  runner.expectTrue(saveMurphyM4Batch(MurphyM4Batch::First) == MurphyM4BatchSaveResult::Saved,
                    "changed selection saves successfully");
  runner.expectTrue(murphyM4Batch() == MurphyM4Batch::Second, "successful save does not change the active boot");
  loadMurphyM4Batch();
  runner.expectTrue(murphyM4Batch() == MurphyM4Batch::First && murphyM4HalfTemperature(murphyM4Batch()) == 0x3C &&
                        mapMurphyM4ShortAxis(0, murphyM4Batch(), 479) == 41,
                    "next boot applies one selection to display and touch");
  for (uint8_t bad : {uint8_t{0}, uint8_t{3}, uint8_t{255}}) {
    store.begin("papyrix_hw");
    store.putUChar("m4_batch", bad);
    store.end();
    loadMurphyM4Batch();
    runner.expectTrue(murphyM4Batch() == MurphyM4Batch::Second, "invalid retained selection uses Batch 2");
  }
  Preferences::failBegin = true;
  loadMurphyM4Batch();
  runner.expectTrue(murphyM4Batch() == MurphyM4Batch::Second, "unreadable preference uses Batch 2");
  Preferences::failBegin = false;
  runner.expectTrue(saveMurphyM4Batch(static_cast<MurphyM4Batch>(0)) == MurphyM4BatchSaveResult::Failed,
                    "invalid selection cannot enter NVS");
  return runner.allPassed() ? 0 : 1;
}
