#include "ui/views/ReaderViews.h"
#include "test_utils.h"

#include <cstring>

int main() {
  TestUtils::TestRunner runner("BookStatsView");

  ui::BookStatsView view;
  runner.expectEq(2, ui::BookStatsView::MAX_TITLE_LINES, "Book Stats title is limited to two lines");
  view.setBook("The Name of the Rose", "Umberto Eco");
  view.setStats(true, 62, 4 * 3600 + 12 * 60, 7);
  runner.expectTrue(strcmp(view.title, "The Name of the Rose") == 0, "title set");
  runner.expectTrue(strcmp(view.author, "Umberto Eco") == 0, "author set");
  runner.expectTrue(strcmp(view.progress, "62%") == 0, "progress formatted");
  runner.expectTrue(strcmp(view.timeRead, "4h 12m") == 0, "duration formatted");
  runner.expectTrue(strcmp(view.sessions, "7") == 0, "sessions formatted");

  view.setStats(false, 0, 0, 0);
  runner.expectTrue(strcmp(view.progress, "—") == 0, "unknown progress uses dash");
  runner.expectTrue(strcmp(view.timeRead, "—") == 0, "zero time uses dash");
  runner.expectTrue(strcmp(view.sessions, "0") == 0, "zero sessions shown");

  char summary[40];
  ui::formatBookStatsSummary(summary, sizeof(summary), true, 8, 65);
  runner.expectTrue(strcmp(summary, "8% · 1m") == 0, "row summary combines progress and time");
  ui::formatBookStatsSummary(summary, sizeof(summary), false, 0, 0);
  runner.expectTrue(strcmp(summary, "—") == 0, "empty summary uses dash");

  return runner.allPassed() ? 0 : 1;
}
