#include <FirmwareBoardTag.h>
#include <TargetConfig.h>

#include <string>

#include "test_utils.h"

using papyrix::firmware_board::Scanner;

namespace {
#if PAPYRIX_TARGET_XTEINK_C3
const std::string expected = "PAPYRIX-BOARD-V1:xteink_c3;";
#elif PAPYRIX_TARGET_X4PRO
const std::string expected = "PAPYRIX-BOARD-V1:x4pro;";
#elif PAPYRIX_TARGET_X4CLASSIC
const std::string expected = "PAPYRIX-BOARD-V1:x4c;";
#elif PAPYRIX_TARGET_MURPHY_M4
const std::string expected = "PAPYRIX-BOARD-V1:murphy_m4;";
#else
const std::string expected = "PAPYRIX-BOARD-V1:sticky;";
#endif
#if PAPYRIX_TARGET_X4PRO
const std::string wrong = "PAPYRIX-BOARD-V1:sticky;";
#else
const std::string wrong = "PAPYRIX-BOARD-V1:x4pro;";
#endif
void feed(Scanner& scanner, const std::string& bytes) {
  scanner.feed(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}
}

int main() {
  TestUtils::TestRunner runner("Firmware board identity");
  for (size_t split = 0; split <= expected.size(); ++split) {
    Scanner scanner;
    feed(scanner, expected.substr(0, split));
    feed(scanner, expected.substr(split));
    scanner.endSegment();
    runner.expectTrue(scanner.compatible(), "matching tag survives chunk split " + std::to_string(split));
  }
  Scanner untagged;
  feed(untagged, "plain image bytes");
  untagged.endSegment();
  runner.expectTrue(untagged.compatible(), "images without a PapyriX board tag remain compatible");
  const std::string prefix = "PAPYRIX-BOARD-V1:";
  for (const auto& bytes :
       {wrong, expected + expected, expected + wrong, prefix + std::string(80, 'x') + ";",
        expected.substr(0, expected.size() - 1), prefix + ";", prefix + std::string(1, '\0') + ";"}) {
    Scanner scanner;
    feed(scanner, bytes);
    scanner.endSegment();
    runner.expectFalse(scanner.compatible(), "foreign, duplicate, or malformed identity is rejected");
  }
  for (size_t split = prefix.size(); split < expected.size(); ++split) {
    Scanner scanner;
    feed(scanner, expected.substr(0, split));
    scanner.endSegment();
    feed(scanner, expected.substr(split));
    scanner.endSegment();
    runner.expectFalse(scanner.compatible(), "separate segments cannot synthesize a tag " + std::to_string(split));
  }
  Scanner overlapping;
  feed(overlapping, "PA" + expected);
  overlapping.endSegment();
  runner.expectTrue(overlapping.compatible(), "overlapping prefix candidate retains the complete identity");
  Scanner trailing;
  feed(trailing, expected);
  trailing.endSegment();
  feed(trailing, "PAPYR");
  trailing.endSegment();
  runner.expectTrue(trailing.compatible(), "partial magic is ordinary data and retains the earlier match");
  Scanner duplicate;
  feed(duplicate, expected);
  duplicate.endSegment();
  feed(duplicate, expected);
  duplicate.endSegment();
  runner.expectFalse(duplicate.compatible(), "duplicate identity in another segment is rejected");
  return runner.allPassed() ? 0 : 1;
}
