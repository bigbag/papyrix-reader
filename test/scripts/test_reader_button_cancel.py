#!/usr/bin/env python3
"""Check cache-wait cancellation and input restoration across reader overlays."""

import subprocess
import tempfile
from pathlib import Path

from test_reader_resume import ROOT, block


HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <memory>
#include <functional>
#include "core/ReaderButtonDispatcher.h"
#include "content/ReaderNavigation.h"
#include "states/State.h"

uint32_t now = 1000;
uint32_t millis() { return now; }
#define LOG_DBG(...) ((void)0)
#define tr(x) #x
struct Theme { int itemHeight = 20, itemSpacing = 2; };
struct Themes { Theme value; const Theme& current() const { return value; } } THEME_MANAGER;
struct Renderer {
  std::function<void()> onPresent;
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  void present() { if (onPresent) onPresent(); }
};
namespace ui {
@TAP_GUARD@;
struct TocHit {
  enum class Type { None, Item, Go, Back, PageUp, PageDown };
  Type type = Type::None;
  int index = 0;
};
TocHit tocHitTest(papyrix::TouchPoint, int, int, int, int, int, int, bool) { return {}; }
template<class View> void render(Renderer& renderer, const Theme&, View&) { renderer.present(); }
void centeredMessage(Renderer& renderer, const Theme&, int, const char*) { renderer.present(); }
}

namespace papyrix {
struct Settings {
  enum { PowerPageTurn, PowerBookmark, NextPrev, FrontLRBC };
  int shortPwrBtn = PowerPageTurn, touchPageTurns = 0, sideButtonLayout = 0, frontButtonLayout = 0;
  int getReaderFontId(const Theme&) const { return 0; }
  uint32_t getPowerButtonDuration() const { return 400; }
};
struct Core {
  Settings settings;
  EventQueue events;
  struct { char text[1] = {}; } buf;
  struct { void setPageButtonsBlocked(bool) {} } input;
};
struct ReaderState {
  uint32_t bookmarkNotifyMs_ = 0, indexingLastRenderMs_ = 0;
  bool needsRender_ = false, loadFailed_ = false, contentLoaded_ = true;
  bool indexingInProgress_ = false, indexingCancelled_ = false, indexingFailed_ = false;
  bool suppressRender_ = false;
  std::unique_ptr<int> indexingCache_, indexingParser_;
  StateId sourceState_ = StateId::Home;
  bool bookStatsMode_ = false, menuMode_ = false, bookmarkMode_ = false, tocMode_ = false;
  bool cacheWaitActive_ = false;
  int currentSpineIndex_ = 1, currentSectionPage_ = 0, bookmarkCount_ = 0;
  int pageTurns = 0, chapterTurns = 0;
  ReaderButtonDispatcher readerButtons_;
  TocInputDispatcher tocInput_;
  ui::OverlayTapGuard overlayTapGuard_;
  Renderer renderer_;
  struct View {
    bool needsRender = false;
    uint16_t selected = 0, chapterCount = 1;
    int scrollOffset = 0;
    void moveUp() {}
    void moveDown() {}
    void movePageUp(int) {}
    void movePageDown(int) {}
  } bookStatsView_, menuView_, tocView_;
  StateTransition update(Core&);
  void render(Core&);
  void beginCacheWait(Core&);
  void endCacheWait(Core&);
  void exitTocMode();
  int tocVisibleCount() const { return 10; }
  void jumpToTocEntry(Core& core, int) { beginCacheWait(core); needsRender_ = true; }
  void renderIndexingScreen(Core&) { assert(false); }
  void renderBookmarkOverlay(Core&) { renderer_.present(); }
  void renderTocOverlay(Core&) { renderer_.present(); }
  void renderCurrentPage(Core& core) { renderer_.present(); endCacheWait(core); }
  void processIndexingChunk(Core&) { assert(false); }
  void startBackgroundCaching(Core&) {}
  void enterMenuMode(Core&) { menuMode_ = true; }
  static void closeOverlay(bool& open, const Event& e) {
    if (e.type == EventType::ButtonPress && e.button == Button::Back) open = false;
  }
  void handleBookStatsInput(Core&, const Event& e) { closeOverlay(bookStatsMode_, e); }
  void handleMenuInput(Core&, const Event& e) { closeOverlay(menuMode_, e); }
  void handleBookmarkInput(Core&, const Event& e) { closeOverlay(bookmarkMode_, e); }
  void handleTocInput(Core&, const Event&);
  void exitToUI(Core&) { assert(false); }
  void navigateNext(Core&) { ++pageTurns; }
  void navigatePrev(Core&) { --pageTurns; }
  void navigateNextChapter(Core&) { ++chapterTurns; }
  void navigatePrevChapter(Core&) { --chapterTurns; }
  void addBookmark(Core&) { ++bookmarkCount_; }
  void showBookmarkNotification(Core&) {}
};

@UPDATE@
}

int main() {
  using namespace papyrix;
  bool ReaderState::*overlays[] = {&ReaderState::bookStatsMode_, &ReaderState::menuMode_,
                                  &ReaderState::bookmarkMode_, &ReaderState::tocMode_};
  const char* names[] = {"statistics", "menu", "bookmarks", "TOC"};
  bool passed = true;
  for (unsigned i = 0; i < 4; ++i) {
    Core core;
    ReaderState state;
    now = 1000;
    assert(core.events.push(Event::buttonPress(Button::Power)));
    assert(core.events.push(Event::buttonPress(Button::Right)));
    assert(core.events.push(Event::buttonRepeat(Button::Right)));
    assert(core.events.push(Event::buttonPress(Button::Center)));
    state.update(core);
    assert(state.chapterTurns == 1 && state.menuMode_ && state.pageTurns == 0);
    state.menuMode_ = false;
    state.*overlays[i] = true;
    now = 1100;
    assert(core.events.push(Event::buttonCancel(Button::Right)));
    state.update(core);
    assert(state.*overlays[i]);
    assert(state.readerButtons_.powerPressStartedMs() == 1000);
    assert(core.events.push(Event::buttonPress(Button::Back)));
    state.update(core);
    assert(!(state.*overlays[i]));
    assert(core.events.push(Event::buttonPress(Button::Right)));
    assert(core.events.push(Event::buttonRelease(Button::Right)));
    state.update(core);
    const bool freshTap = state.pageTurns == 1 && state.chapterTurns == 1;
    std::printf("%s: fresh page tap after cancellation: %s\n", names[i], freshTap ? "PASS" : "FAIL");
    passed &= freshTap;
    now = 1200;
    assert(core.events.push(Event::buttonRelease(Button::Power)));
    state.update(core);
    passed &= state.pageTurns == 2;
  }
  {
    Core core;
    ReaderState state;
    state.tocMode_ = true;
    assert(core.events.push(Event::buttonPress(Button::Center)));
    assert(core.events.push(Event::tap({240, 400})));
    state.update(core);
    assert(state.menuMode_ && !state.tocMode_ && state.cacheWaitActive_);
    unsigned presentations = 0;
    state.renderer_.onPresent = [&] {
      ++presentations;
      assert(state.cacheWaitActive_);
    };
    state.render(core);
    const bool waitEnded = !state.cacheWaitActive_ && presentations == 1;
    std::printf("TOC select then center tap: menu ends cache wait: %s\n", waitEnded ? "PASS" : "FAIL");
    passed &= waitEnded;
    for (auto overlay : overlays) {
      state.menuMode_ = false;
      state.*overlay = true;
      state.cacheWaitActive_ = true;
      state.needsRender_ = true;
      state.render(core);
      passed &= !state.cacheWaitActive_;
      state.*overlay = false;
    }
  }
  return passed ? 0 : 1;
}
'''


def main():
    reader = (ROOT / "src/states/ReaderState.cpp").read_text()
    methods = ["StateTransition ReaderState::update(", "void ReaderState::render(",
               "void ReaderState::beginCacheWait(", "void ReaderState::endCacheWait(",
               "void ReaderState::handleTocInput(", "void ReaderState::exitTocMode("]
    harness = HARNESS.replace("@UPDATE@", "\n".join(block(reader, marker) for marker in methods))
    views = (ROOT / "src/ui/views/ReaderViews.h").read_text()
    harness = harness.replace("@TAP_GUARD@", block(views, "class OverlayTapGuard"))
    with tempfile.TemporaryDirectory(prefix="papyrix-reader-cancel-") as directory:
        source = Path(directory) / "cancel.cpp"
        binary = Path(directory) / "cancel"
        source.write_text(harness)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
                        str(source), str(ROOT / "src/core/ReaderButtonDispatcher.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
