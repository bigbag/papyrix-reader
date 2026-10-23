#include <BoardSelector.h>
#include <HardwareIdentity.h>

#include <cstring>

#include "test_utils.h"

using papyrix::board::BoardId;
using papyrix::board::BoardProbeReport;
using papyrix::board::BoardProbeVerdict;
using papyrix::board::BoardSelectionSource;
using papyrix::board::classifyBoardProbe;
using papyrix::board::HardwareIdentity;
using papyrix::board::kBoardCacheKey;
using papyrix::board::kBoardOverrideKey;
using papyrix::board::kControllerCacheKey;
using papyrix::board::kControllerOverrideKey;
using papyrix::board::kControllerVersionKey;
using papyrix::board::kHardwareNamespace;
using papyrix::board::kStoredAuto;
using papyrix::board::kStoredX3;
using papyrix::board::kStoredX4;
using papyrix::board::PanelSelectionSource;
using papyrix::board::resolveBoardSelection;
using papyrix::eink::DisplayController;

namespace {

BoardProbeReport report(uint8_t pass1, uint8_t pass2) {
  BoardProbeReport value{};
  value.pass1.score = pass1;
  value.pass2.score = pass2;
  return value;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("Board selector policy");

  runner.expectTrue(std::strcmp(kHardwareNamespace, "papyrix_hw") == 0, "hardware namespace remains compatible");
  runner.expectTrue(std::strcmp(kBoardOverrideKey, "dev_ovr") == 0, "board override key remains compatible");
  runner.expectTrue(std::strcmp(kBoardCacheKey, "dev_det") == 0, "board cache key remains compatible");
  runner.expectTrue(std::strcmp(kControllerOverrideKey, "epd_ovr") == 0, "controller override key remains compatible");
  runner.expectTrue(std::strcmp(kControllerCacheKey, "epd_det") == 0, "controller cache key remains compatible");
  runner.expectTrue(std::strcmp(kControllerVersionKey, "epd_ver") == 0, "controller version key remains compatible");
  runner.expectEq(0, static_cast<int>(kStoredAuto), "automatic board value remains zero");
  runner.expectEq(1, static_cast<int>(kStoredX4), "X4 board value remains one");
  runner.expectEq(2, static_cast<int>(kStoredX3), "X3 board value remains two");

  runner.expectEq(static_cast<int>(BoardProbeVerdict::X3), static_cast<int>(classifyBoardProbe(report(2, 2))),
                  "two passes with two signatures select X3");
  runner.expectEq(static_cast<int>(BoardProbeVerdict::X3), static_cast<int>(classifyBoardProbe(report(3, 2))),
                  "scores above threshold select X3");
  runner.expectEq(static_cast<int>(BoardProbeVerdict::X4), static_cast<int>(classifyBoardProbe(report(0, 0))),
                  "two empty passes select X4");
  runner.expectEq(static_cast<int>(BoardProbeVerdict::Inconclusive), static_cast<int>(classifyBoardProbe(report(2, 0))),
                  "pass disagreement stays inconclusive");
  runner.expectEq(static_cast<int>(BoardProbeVerdict::Inconclusive), static_cast<int>(classifyBoardProbe(report(1, 1))),
                  "weak repeated signatures stay inconclusive");

  auto decision = resolveBoardSelection(kStoredX3, kStoredX4, BoardProbeVerdict::X4);
  runner.expectEq(static_cast<int>(BoardId::X3), static_cast<int>(decision.board), "override wins over cache");
  runner.expectEq(static_cast<int>(BoardSelectionSource::Override), static_cast<int>(decision.source),
                  "override source is reported");
  runner.expectFalse(decision.writeCache, "override does not rewrite automatic cache");

  decision = resolveBoardSelection(kStoredAuto, kStoredX4, BoardProbeVerdict::X3);
  runner.expectEq(static_cast<int>(BoardId::X4), static_cast<int>(decision.board), "cache wins over live probe");
  runner.expectEq(static_cast<int>(BoardSelectionSource::Cache), static_cast<int>(decision.source),
                  "cache source is reported");

  decision = resolveBoardSelection(9, kStoredX3, BoardProbeVerdict::X4);
  runner.expectTrue(decision.invalidOverride, "invalid override is diagnosed");
  runner.expectEq(static_cast<int>(BoardId::X3), static_cast<int>(decision.board),
                  "valid cache follows invalid override");

  decision = resolveBoardSelection(kStoredAuto, 9, BoardProbeVerdict::X3);
  runner.expectTrue(decision.invalidCache, "invalid cache is diagnosed");
  runner.expectEq(static_cast<int>(BoardId::X3), static_cast<int>(decision.board),
                  "confirmed probe follows invalid cache");
  runner.expectEq(static_cast<int>(BoardSelectionSource::Probe), static_cast<int>(decision.source),
                  "confirmed probe source is reported");
  runner.expectTrue(decision.writeCache, "confirmed X3 probe writes cache");
  runner.expectEq(static_cast<int>(kStoredX3), static_cast<int>(decision.storedValue),
                  "confirmed X3 writes the existing stored value");

  decision = resolveBoardSelection(kStoredAuto, kStoredAuto, BoardProbeVerdict::X4);
  runner.expectEq(static_cast<int>(BoardId::X4), static_cast<int>(decision.board), "confirmed X4 probe resolves");
  runner.expectTrue(decision.writeCache, "confirmed X4 probe writes cache");
  runner.expectEq(static_cast<int>(kStoredX4), static_cast<int>(decision.storedValue),
                  "confirmed X4 writes the existing stored value");

  decision = resolveBoardSelection(kStoredAuto, kStoredAuto, BoardProbeVerdict::Inconclusive);
  runner.expectEq(static_cast<int>(BoardId::X4), static_cast<int>(decision.board),
                  "inconclusive probe falls back to X4 for this boot");
  runner.expectEq(static_cast<int>(BoardSelectionSource::Fallback), static_cast<int>(decision.source),
                  "inconclusive source is fallback");
  runner.expectFalse(decision.writeCache, "inconclusive fallback is not cached");

  HardwareIdentity identity;
  runner.expectEq(static_cast<int>(BoardId::X4), static_cast<int>(identity.board()),
                  "C3 identity starts with the safe X4 boot profile");
  runner.expectTrue(std::strcmp(identity.profile().cacheDir, "/.papyrix/cache") == 0,
                    "default identity exposes the X4 cache directory");
  runner.expectFalse(identity.boardSelected(), "C3 identity stays unselected until board policy runs");

  const BoardProbeReport x3Report = report(3, 2);
  decision = resolveBoardSelection(kStoredAuto, kStoredAuto, BoardProbeVerdict::X3);
  runner.expectTrue(identity.applyBoardSelection(decision, x3Report), "linked X3 selection applies");
  runner.expectTrue(identity.boardSelected(), "applied board selection is observable");
  runner.expectEq(static_cast<int>(BoardId::X3), static_cast<int>(identity.board()),
                  "identity switches to the X3 profile");
  runner.expectEq(static_cast<int>(BoardSelectionSource::Probe), static_cast<int>(identity.boardSource()),
                  "identity stores the board selection source");
  runner.expectEq(2, static_cast<int>(identity.lastProbe().pass2.score), "identity stores the complete probe report");
  runner.expectTrue(std::strcmp(identity.cacheDir(), "/.papyrix/cache/x3") == 0,
                    "identity exposes the selected profile cache directory");
  runner.expectFalse(
      identity.applyBoardSelection({BoardId::X4Pro, BoardSelectionSource::Fixed, false, kStoredAuto, false, false}, {}),
      "identity rejects a profile not linked into this artifact");
  runner.expectEq(static_cast<int>(BoardId::X3), static_cast<int>(identity.board()),
                  "rejected selection leaves the active profile unchanged");

  identity.setPanel(DisplayController::UC8279_X3, PanelSelectionSource::Probe);
  runner.expectEq(static_cast<int>(DisplayController::UC8279_X3), static_cast<int>(identity.panel()),
                  "identity stores the selected panel controller");
  runner.expectEq(static_cast<int>(PanelSelectionSource::Probe), static_cast<int>(identity.panelSource()),
                  "identity stores the panel selection source");

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
