#include <I18n.h>

#include <string>

#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("Translation identifier boundaries");
  auto& i18n = I18n::instance();
  const std::string back = i18n.get(StrId::STR_BACK);
  const std::string open = i18n.get(StrId::STR_OPEN);
  const std::string twoMinutes = i18n.get(StrId::STR_MIN_2);

  runner.expectTrue(i18n.setOverride("MIN_1", "one-minute-override"), "override below the byte boundary is accepted");
  runner.expectTrue(i18n.setOverride("MIN_2", "two-minute-override"), "override above the byte boundary is accepted");
  runner.expectEq(std::string("one-minute-override"), std::string(i18n.get(StrId::STR_MIN_1)),
                  "adjacent duration translations stay independent");
  runner.expectEq(std::string("two-minute-override"), std::string(i18n.get(StrId::STR_MIN_2)),
                  "the high identifier returns its own translation");
  runner.expectEq(back, std::string(i18n.get(StrId::STR_BACK)), "high identifiers do not replace the Back label");
  runner.expectEq(open, std::string(i18n.get(StrId::STR_OPEN)), "high identifiers do not replace the Open label");

  i18n.resetToDefaults();
  runner.expectEq(twoMinutes, std::string(i18n.get(StrId::STR_MIN_2)), "reset restores the high identifier default");
  return runner.allPassed() ? 0 : 1;
}
