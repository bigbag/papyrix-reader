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

#include "../content/RecentBooksStore.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

namespace papyrix {

void RecentState::enter(Core& core) {
  RecentBooksStore& store = RecentBooksStore::instance();
  store.load();
  size_t before = store.books().size();
  store.pruneMissing();
  store.trimTo(visibleCount());
  if (store.books().size() != before) {
    store.save();
  }
  selected_ = 0;
  needsRender_ = true;
}

void RecentState::moveUp() {
  const auto& books = RecentBooksStore::instance().books();
  if (!books.empty() && selected_ > 0) {
    selected_--;
    needsRender_ = true;
  }
}

void RecentState::moveDown() {
  const auto& books = RecentBooksStore::instance().books();
  if (selected_ + 1 < books.size()) {
    selected_++;
    needsRender_ = true;
  }
}

StateTransition RecentState::openSelected(Core& core) {
  const auto& books = RecentBooksStore::instance().books();
  if (selected_ >= books.size()) {
    return StateTransition::stay(StateId::Recent);
  }
  const std::string path = books[selected_].path;

  // Tap-guard: file vanished since the list was rendered. Prune + refresh, don't reboot.
  if (!SdMan.exists(path.c_str())) {
    RecentBooksStore::instance().remove(path);
    if (selected_ > 0 && selected_ >= RecentBooksStore::instance().books().size()) {
      selected_--;
    }
    needsRender_ = true;
    return StateTransition::stay(StateId::Recent);
  }

  LOG_INF("RECENT", "Opening recent book: %s", path.c_str());
  showTransitionNotification(tr(OPENING_BOOK));
  saveTransition(BootMode::READER, path.c_str(), ReturnTo::RECENT);
  vTaskDelay(50 / portTICK_PERIOD_MS);
  ESP.restart();
  return StateTransition::stay(StateId::Recent);  // unreachable
}

StateTransition RecentState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonRepeat:
        if (currentScreen_ == Screen::Browse) {
          if (e.button == Button::Up) {
            moveUp();
          } else if (e.button == Button::Down) {
            moveDown();
          }
        }
        break;

      case EventType::ButtonPress:
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
                confirmRemove(core);
              }
              currentScreen_ = Screen::Browse;
              needsRender_ = true;
              break;
            case Button::Back:
              currentScreen_ = Screen::Browse;
              needsRender_ = true;
              break;
            default:
              break;
          }
        } else {
          switch (e.button) {
            case Button::Up:
              moveUp();
              break;
            case Button::Down:
              moveDown();
              break;
            case Button::Center:
              return openSelected(core);
            case Button::Left:
              return StateTransition::to(StateId::FileList);
            case Button::Right:
              promptRemove(core);
              break;
            case Button::Back:
              return StateTransition::to(StateId::Home);
            default:
              break;
          }
        }
        break;

      default:
        break;
    }
  }

  return StateTransition::stay(StateId::Recent);
}

void RecentState::render(Core& core) {
  if (!needsRender_) {
    return;
  }
  needsRender_ = false;

  const Theme& theme = THEME;

  if (currentScreen_ == Screen::ConfirmRemove) {
    ui::render(renderer_, theme, confirmView_);
    confirmView_.needsRender = false;
    core.display.markDirty();
    return;
  }

  renderer_.clearScreen(theme.backgroundColor);

  // Title
  renderer_.drawCenteredText(theme.uiFontId, 10, tr(BOOKS), theme.primaryTextBlack, BOLD);
  // Section label
  renderer_.drawCenteredText(theme.smallFontId, 36, tr(RECENT_BOOKS), theme.secondaryTextBlack);

  const auto& books = RecentBooksStore::instance().books();

  if (books.empty()) {
    renderer_.drawText(theme.uiFontId, theme.screenMarginSide + 8, 70, tr(NO_RECENT_BOOKS), theme.secondaryTextBlack);
  } else {
    constexpr int listStartY = RecentBooksStore::LIST_START_Y;
    const int rowPitch = rowHeight();
    const int x = theme.screenMarginSide;
    const int w = renderer_.getScreenWidth() - 2 * theme.screenMarginSide;
    const int textX = x + theme.itemPaddingX;
    const int maxTextW = w - 2 * theme.itemPaddingX;
    const int titleLH = renderer_.getLineHeight(theme.uiFontId);

    const int visible = visibleCount();
    int end = std::min<int>(visible, static_cast<int>(books.size()));

    for (int i = 0; i < end; i++) {
      const int y = listStartY + i * rowPitch;
      const bool sel = (static_cast<size_t>(i) == selected_);

      if (sel) {
        renderer_.fillRect(x, y, w, rowPitch - 2, theme.selectionFillBlack);
      }
      const bool titleBlack = sel ? theme.selectionTextBlack : theme.primaryTextBlack;
      const std::string title = renderer_.truncatedText(theme.uiFontId, books[i].title.c_str(), maxTextW);
      renderer_.drawText(theme.uiFontId, textX, y + 2, title.c_str(), titleBlack);

      if (!books[i].author.empty()) {
        const bool authorBlack = sel ? theme.selectionTextBlack : theme.secondaryTextBlack;
        const std::string author = renderer_.truncatedText(theme.uiFontId, books[i].author.c_str(), maxTextW);
        renderer_.drawText(theme.uiFontId, textX, y + 2 + titleLH, author.c_str(), authorBlack);
      }
    }
  }

  // Back / Open / Files / Delete
  ui::buttonBar(renderer_, theme, tr(BACK), tr(OPEN), tr(FILES), tr(DELETE_BTN));

  renderer_.displayBuffer();
  core.display.markDirty();
}

void RecentState::promptRemove(Core& core) {
  const auto& books = RecentBooksStore::instance().books();
  if (books.empty() || selected_ >= books.size()) {
    return;
  }

  char line[48];
  const std::string& title = books[selected_].title;
  // Truncate at a UTF-8 boundary (reserving 3 bytes for "...") so multibyte titles
  // (CJK/Thai/Arabic) are not split mid-character in the confirm dialog.
  size_t n = utf8SafeCopy(line, sizeof(line) - 3, title.c_str());
  if (n < title.size()) {
    line[n++] = '.';
    line[n++] = '.';
    line[n++] = '.';
  }
  line[n] = '\0';

  confirmView_.setup(tr(REMOVE_FROM_RECENT_Q), line, nullptr);
  currentScreen_ = Screen::ConfirmRemove;
  needsRender_ = true;
}

void RecentState::confirmRemove(Core& core) {
  const auto& books = RecentBooksStore::instance().books();
  if (selected_ >= books.size()) {
    return;
  }
  std::string path = books[selected_].path;  // copy before remove invalidates the list ref
  RecentBooksStore::instance().remove(path);

  const size_t count = RecentBooksStore::instance().books().size();
  if (count == 0) {
    selected_ = 0;
  } else if (selected_ >= count) {
    selected_--;
  }
}

int RecentState::rowHeight() const { return RecentBooksStore::rowHeight(renderer_.getLineHeight(THEME.uiFontId)); }

int RecentState::visibleCount() const {
  const int available = renderer_.getScreenHeight() - RecentBooksStore::LIST_START_Y - RecentBooksStore::BOTTOM_MARGIN;
  return std::max(1, available / rowHeight());
}

}  // namespace papyrix
