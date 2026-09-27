#include "RecentState.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <Theme.h>
#include <Utf8.h>
#include <esp_system.h>

#include <algorithm>

#include "../content/ReadingStatsStore.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

namespace papyrix {

void RecentState::enter(Core& core) {
  auto& recent = RecentBooksStore::instance();
  recent.load();
  const size_t before = recent.books().size();
  recent.pruneMissing();
  if (recent.books().size() != before) recent.save();
  READING_STATS.load();
  selected_ = 0;
  currentScreen_ = Screen::Browse;
  needsRender_ = true;
}

void RecentState::moveUp() {
  if (selected_ > 0) {
    selected_--;
    needsRender_ = true;
  }
}

void RecentState::moveDown() {
  const size_t count = displayedCount();
  if (selected_ + 1 < count) {
    selected_++;
    needsRender_ = true;
  }
}

StateTransition RecentState::openSelected(Core& core) {
  const auto& books = RecentBooksStore::instance().books();
  if (selected_ >= displayedCount()) {
    return StateTransition::stay(StateId::Recent);
  }
  const std::string path = books[selected_].path;

  if (!SdMan.exists(path.c_str())) {
    removeSelected();
    return StateTransition::stay(StateId::Recent);
  }

  LOG_INF("RECENT", "Opening recent book: %s", path.c_str());
  showTransitionNotification(tr(OPENING_BOOK));
  saveTransition(BootMode::READER, path.c_str(), ReturnTo::RECENT);
  vTaskDelay(50 / portTICK_PERIOD_MS);
  ESP.restart();
  return StateTransition::stay(StateId::Recent);
}

StateTransition RecentState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    if (e.type == EventType::ButtonRepeat && currentScreen_ == Screen::Browse) {
      if (e.button == Button::Up) {
        moveUp();
      } else if (e.button == Button::Down) {
        moveDown();
      }
      continue;
    }
    if (e.type == EventType::Tap && currentScreen_ == Screen::Browse) {
      const ui::RecentHit hit = ui::recentHitTest(
          {e.touch.x, e.touch.y}, renderer_.getScreenWidth(), renderer_.getScreenHeight(), rowHeight(),
          static_cast<int>(displayedCount()), core.settings.frontButtonLayout == Settings::FrontLRBC);
      if (hit.type == ui::RecentHit::Type::Entry) {
        selected_ = static_cast<size_t>(hit.index);
        needsRender_ = true;
        return openSelected(core);
      }
      if (hit.type == ui::RecentHit::Type::Open) return openSelected(core);
      if (hit.type == ui::RecentHit::Type::Files) return StateTransition::to(StateId::FileList);
      if (hit.type == ui::RecentHit::Type::Info) showSelectedStats();
      if (hit.type == ui::RecentHit::Type::Back) return StateTransition::to(StateId::Home);
      continue;
    }
    if (e.type == EventType::Tap && currentScreen_ == Screen::Stats) {
      const auto hit =
          statsView_.hitTest({e.touch.x, e.touch.y}, renderer_.getScreenWidth(), renderer_.getScreenHeight(),
                             core.settings.frontButtonLayout == Settings::FrontLRBC);
      if (hit == ui::BookStatsView::Hit::None) continue;
      if (hit == ui::BookStatsView::Hit::Back) e = Event::buttonPress(Button::Back);
      if (hit == ui::BookStatsView::Hit::Open) e = Event::buttonPress(Button::Center);
      if (hit == ui::BookStatsView::Hit::Remove) e = Event::buttonPress(Button::Right);
    }
    if (e.type == EventType::Tap && currentScreen_ == Screen::ConfirmRemove) {
      const auto layout = ui::confirmDialogBounds(renderer_, THEME, confirmView_);
      const auto hit =
          confirmView_.hitTest({e.touch.x, e.touch.y}, layout, core.settings.frontButtonLayout == Settings::FrontLRBC);
      if (hit == ui::ConfirmDialogView::Hit::Yes) {
        removeSelected();
      } else if (hit == ui::ConfirmDialogView::Hit::No || hit == ui::ConfirmDialogView::Hit::Back) {
        currentScreen_ = Screen::Stats;
        needsRender_ = true;
      } else if (hit == ui::ConfirmDialogView::Hit::Select) {
        e = Event::buttonPress(Button::Center);
      }
      if (hit != ui::ConfirmDialogView::Hit::Select) continue;
    }
    if (e.type != EventType::ButtonPress) continue;

    if (currentScreen_ == Screen::ConfirmRemove) {
      switch (e.button) {
        case Button::Up:
        case Button::Down:
        case Button::Left:
        case Button::Right:
          confirmView_.toggleSelection();
          needsRender_ = true;
          break;
        case Button::Center:
          if (confirmView_.isYesSelected()) {
            removeSelected();
          } else {
            currentScreen_ = Screen::Stats;
            needsRender_ = true;
          }
          break;
        case Button::Back:
          currentScreen_ = Screen::Stats;
          needsRender_ = true;
          break;
        default:
          break;
      }
      continue;
    }

    if (currentScreen_ == Screen::Stats) {
      if (e.button == Button::Back) {
        currentScreen_ = Screen::Browse;
        needsRender_ = true;
      } else if (e.button == Button::Center) {
        return openSelected(core);
      } else if (e.button == Button::Right) {
        confirmRemove();
      }
      continue;
    }

    switch (e.button) {
      case Button::Up:
        moveUp();
        break;
      case Button::Down:
        moveDown();
        break;
      case Button::Center:
        return openSelected(core);
      case FILES_BUTTON:
        return StateTransition::to(StateId::FileList);
      case INFO_BUTTON:
        showSelectedStats();
        break;
      case Button::Back:
        return StateTransition::to(StateId::Home);
      default:
        break;
    }
  }

  return StateTransition::stay(StateId::Recent);
}

void RecentState::render(Core& core) {
  if (!needsRender_) return;
  needsRender_ = false;

  if (currentScreen_ == Screen::Stats) {
    renderStats(core);
  } else if (currentScreen_ == Screen::ConfirmRemove) {
    ui::render(renderer_, THEME, confirmView_);
  } else {
    renderBrowse(core);
  }
}

void RecentState::showSelectedStats() {
  const auto& books = RecentBooksStore::instance().books();
  if (selected_ >= displayedCount()) return;

  const auto& book = books[selected_];
  statsView_.setBook(book.title.c_str(), book.author.c_str());
  const ReadingStatsRecord* stats = READING_STATS.find(book.path);
  if (stats) {
    statsView_.setStats(stats->hasProgress, stats->progressPercent, stats->totalSeconds, stats->sessionCount);
  } else {
    statsView_.setStats(false, 0, 0, 0);
  }
  statsView_.showOpen = true;
  statsView_.showRemove = true;
  currentScreen_ = Screen::Stats;
  needsRender_ = true;
}

void RecentState::confirmRemove() {
  const auto& books = RecentBooksStore::instance().books();
  if (selected_ >= displayedCount()) return;

  char title[ui::ConfirmDialogView::MAX_LINE_LEN];
  const size_t length = utf8SafeCopy(title, sizeof(title) - 4, books[selected_].title.c_str());
  if (length < books[selected_].title.size()) {
    title[length] = '.';
    title[length + 1] = '.';
    title[length + 2] = '.';
    title[length + 3] = '\0';
  }
  confirmView_.setup(tr(REMOVE_RECENT_Q), tr(CLEAR_RECENT_MSG), title);
  currentScreen_ = Screen::ConfirmRemove;
  needsRender_ = true;
}

void RecentState::removeSelected() {
  const auto& books = RecentBooksStore::instance().books();
  if (selected_ >= displayedCount()) return;

  const std::string path = books[selected_].path;
  RecentBooksStore::instance().remove(path);
  const size_t count = displayedCount();
  if (selected_ > 0 && selected_ >= count) selected_--;
  currentScreen_ = Screen::Browse;
  needsRender_ = true;
}

void RecentState::renderBrowse(Core& core) {
  const Theme& theme = THEME;
  renderer_.clearScreen(theme.backgroundColor);
  renderer_.drawCenteredText(theme.uiFontId, 10, tr(BOOKS), theme.primaryTextBlack, BOLD);
  renderer_.drawCenteredText(theme.smallFontId, 36, tr(RECENT_BOOKS), theme.secondaryTextBlack);

  const auto& books = RecentBooksStore::instance().books();
  if (books.empty()) {
    renderer_.drawText(theme.uiFontId, theme.screenMarginSide + 8, 70, tr(NO_RECENT_BOOKS), theme.secondaryTextBlack);
  } else {
    const int rowPitch = rowHeight();
    const int x = theme.screenMarginSide;
    const int w = renderer_.getScreenWidth() - 2 * theme.screenMarginSide;
    const int textX = x + theme.itemPaddingX;
    const int maxTextW = w - 2 * theme.itemPaddingX;
    const int titleLineHeight = renderer_.getLineHeight(theme.uiFontId);
    const size_t count = displayedCount();

    for (size_t i = 0; i < count; i++) {
      const int y = ui::RecentHit::LIST_START_Y + static_cast<int>(i) * rowPitch;
      const bool selected = i == selected_;
      if (selected) renderer_.fillRect(x, y, w, rowPitch - 2, theme.selectionFillBlack);

      const bool titleBlack = selected ? theme.selectionTextBlack : theme.primaryTextBlack;
      const bool detailBlack = selected ? theme.selectionTextBlack : theme.secondaryTextBlack;
      const auto& book = books[i];
      const std::string title = renderer_.truncatedText(theme.uiFontId, book.title.c_str(), maxTextW);
      renderer_.drawText(theme.uiFontId, textX, y + 2, title.c_str(), titleBlack);

      const ReadingStatsRecord* stats = READING_STATS.find(book.path);
      char summary[40];
      ui::formatBookStatsSummary(summary, sizeof(summary), stats && stats->hasProgress,
                                 stats ? stats->progressPercent : 0, stats ? stats->totalSeconds : 0);
      const int summaryWidth = renderer_.getTextWidth(theme.smallFontId, summary);
      const int detailY = y + 2 + titleLineHeight;
      const int authorWidth = maxTextW - summaryWidth - 12;
      if (!book.author.empty() && authorWidth > 0) {
        const std::string author = renderer_.truncatedText(theme.smallFontId, book.author.c_str(), authorWidth);
        renderer_.drawText(theme.smallFontId, textX, detailY, author.c_str(), detailBlack);
      }
      renderer_.drawText(theme.smallFontId, textX + maxTextW - summaryWidth, detailY, summary, detailBlack);
    }
  }

  ui::buttonBar(renderer_, theme, tr(BACK), tr(OPEN), tr(FILES), tr(INFO));
  renderer_.displayBuffer();
}

void RecentState::renderStats(Core& core) {
  ui::render(renderer_, THEME, statsView_);
  statsView_.needsRender = false;
}

size_t RecentState::displayedCount() const {
  return RecentBooksStore::displayCount(RecentBooksStore::instance().books().size(), renderer_.getScreenHeight(),
                                        rowHeight());
}

int RecentState::rowHeight() const { return RecentBooksStore::rowHeight(renderer_.getLineHeight(THEME.uiFontId)); }

}  // namespace papyrix
