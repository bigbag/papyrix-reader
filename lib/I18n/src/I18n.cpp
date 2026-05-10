#include "I18n.h"

#include <cstring>

#include "I18nDefaults.h"

namespace {

struct KeyMapping {
  const char* name;
  StrId id;
};

// clang-format off
static constexpr KeyMapping KEY_MAP[] = {
    {"BACK", StrId::STR_BACK},
    {"OPEN", StrId::STR_OPEN},
    {"SELECT", StrId::STR_SELECT},
    {"CANCEL", StrId::STR_CANCEL},
    {"CONFIRM", StrId::STR_CONFIRM},
    {"DONE", StrId::STR_DONE},
    {"RETRY", StrId::STR_RETRY},
    {"STOP", StrId::STR_STOP},
    {"SCAN", StrId::STR_SCAN},
    {"RUN", StrId::STR_RUN},
    {"GO", StrId::STR_GO},
    {"RESTART", StrId::STR_RESTART},
    {"CONNECT", StrId::STR_CONNECT},
    {"READ", StrId::STR_READ},
    {"DELETE_BTN", StrId::STR_DELETE_BTN},
    {"FILE", StrId::STR_FILE},
    {"APPS", StrId::STR_APPS},
    {"HOME", StrId::STR_HOME},
    {"YES", StrId::STR_YES},
    {"NO", StrId::STR_NO},
    {"SETTINGS", StrId::STR_SETTINGS},
    {"READER", StrId::STR_READER},
    {"DEVICE", StrId::STR_DEVICE},
    {"CLEANUP", StrId::STR_CLEANUP},
    {"SYSTEM_INFO", StrId::STR_SYSTEM_INFO},
    {"CHAPTERS", StrId::STR_CHAPTERS},
    {"BOOKMARKS", StrId::STR_BOOKMARKS},
    {"JOIN_NETWORK", StrId::STR_JOIN_NETWORK},
    {"CREATE_HOTSPOT", StrId::STR_CREATE_HOTSPOT},
    {"WIFI_TRANSFER", StrId::STR_WIFI_TRANSFER},
    {"CALIBRE_SYNC", StrId::STR_CALIBRE_SYNC},
    {"LANGUAGE", StrId::STR_LANGUAGE},
    {"READER_SETTINGS", StrId::STR_READER_SETTINGS},
    {"DEVICE_SETTINGS", StrId::STR_DEVICE_SETTINGS},
    {"FILES", StrId::STR_FILES},
    {"NETWORK_MODE", StrId::STR_NETWORK_MODE},
    {"SELECT_NETWORK", StrId::STR_SELECT_NETWORK},
    {"CONNECTING_TITLE", StrId::STR_CONNECTING_TITLE},
    {"WEB_SERVER", StrId::STR_WEB_SERVER},
    {"GO_TO_PAGE", StrId::STR_GO_TO_PAGE},
    {"MENU", StrId::STR_MENU},
    {"THEME", StrId::STR_THEME},
    {"FONT_SIZE", StrId::STR_FONT_SIZE},
    {"TEXT_LAYOUT", StrId::STR_TEXT_LAYOUT},
    {"LINE_SPACING", StrId::STR_LINE_SPACING},
    {"TEXT_ANTI_ALIASING", StrId::STR_TEXT_ANTI_ALIASING},
    {"PARAGRAPH_ALIGNMENT", StrId::STR_PARAGRAPH_ALIGNMENT},
    {"HYPHENATION", StrId::STR_HYPHENATION},
    {"SHOW_IMAGES", StrId::STR_SHOW_IMAGES},
    {"STATUS_BAR", StrId::STR_STATUS_BAR},
    {"READING_ORIENTATION", StrId::STR_READING_ORIENTATION},
    {"AUTO_SLEEP_TIMEOUT", StrId::STR_AUTO_SLEEP_TIMEOUT},
    {"SLEEP_SCREEN", StrId::STR_SLEEP_SCREEN},
    {"STARTUP_BEHAVIOR", StrId::STR_STARTUP_BEHAVIOR},
    {"SHORT_POWER_BUTTON", StrId::STR_SHORT_POWER_BUTTON},
    {"PAGES_PER_REFRESH", StrId::STR_PAGES_PER_REFRESH},
    {"SUNLIGHT_FADING_FIX", StrId::STR_SUNLIGHT_FADING_FIX},
    {"FRONT_BUTTONS", StrId::STR_FRONT_BUTTONS},
    {"SIDE_BUTTONS", StrId::STR_SIDE_BUTTONS},
    {"FULL_BOOK_PROCESS", StrId::STR_FULL_BOOK_PROCESS},
    {"ON", StrId::STR_ON},
    {"OFF", StrId::STR_OFF},
    {"XSMALL", StrId::STR_XSMALL},
    {"SMALL", StrId::STR_SMALL},
    {"NORMAL", StrId::STR_NORMAL},
    {"LARGE", StrId::STR_LARGE},
    {"COMPACT", StrId::STR_COMPACT},
    {"STANDARD", StrId::STR_STANDARD},
    {"RELAXED", StrId::STR_RELAXED},
    {"JUSTIFIED", StrId::STR_JUSTIFIED},
    {"LEFT", StrId::STR_LEFT},
    {"CENTER", StrId::STR_CENTER},
    {"RIGHT", StrId::STR_RIGHT},
    {"NONE_VAL", StrId::STR_NONE_VAL},
    {"TITLE_VAL", StrId::STR_TITLE_VAL},
    {"CHAPTER_VAL", StrId::STR_CHAPTER_VAL},
    {"PORTRAIT", StrId::STR_PORTRAIT},
    {"LANDSCAPE_CW", StrId::STR_LANDSCAPE_CW},
    {"INVERTED", StrId::STR_INVERTED},
    {"LANDSCAPE_CCW", StrId::STR_LANDSCAPE_CCW},
    {"DARK", StrId::STR_DARK},
    {"LIGHT", StrId::STR_LIGHT},
    {"CUSTOM", StrId::STR_CUSTOM},
    {"COVER", StrId::STR_COVER},
    {"LAST_DOCUMENT", StrId::STR_LAST_DOCUMENT},
    {"IGNORE", StrId::STR_IGNORE},
    {"SLEEP_VAL", StrId::STR_SLEEP_VAL},
    {"PAGE_TURN", StrId::STR_PAGE_TURN},
    {"NEVER", StrId::STR_NEVER},
    {"MIN_5", StrId::STR_MIN_5},
    {"MIN_10", StrId::STR_MIN_10},
    {"MIN_15", StrId::STR_MIN_15},
    {"MIN_30", StrId::STR_MIN_30},
    {"PREV_NEXT", StrId::STR_PREV_NEXT},
    {"NEXT_PREV", StrId::STR_NEXT_PREV},
    {"FRONT_BCLR", StrId::STR_FRONT_BCLR},
    {"FRONT_LRBC", StrId::STR_FRONT_LRBC},
    {"CLEAR_BOOK_CACHE", StrId::STR_CLEAR_BOOK_CACHE},
    {"CLEAR_DEVICE_STORAGE", StrId::STR_CLEAR_DEVICE_STORAGE},
    {"FACTORY_RESET", StrId::STR_FACTORY_RESET},
    {"CLEAR_CACHES_Q", StrId::STR_CLEAR_CACHES_Q},
    {"CLEAR_CACHES_MSG1", StrId::STR_CLEAR_CACHES_MSG1},
    {"CLEAR_CACHES_MSG2", StrId::STR_CLEAR_CACHES_MSG2},
    {"CLEAR_DEVICE_Q", StrId::STR_CLEAR_DEVICE_Q},
    {"CLEAR_DEVICE_MSG1", StrId::STR_CLEAR_DEVICE_MSG1},
    {"CLEAR_DEVICE_MSG2", StrId::STR_CLEAR_DEVICE_MSG2},
    {"FACTORY_RESET_Q", StrId::STR_FACTORY_RESET_Q},
    {"FACTORY_RESET_MSG1", StrId::STR_FACTORY_RESET_MSG1},
    {"FACTORY_RESET_MSG2", StrId::STR_FACTORY_RESET_MSG2},
    {"CLEARING_CACHE", StrId::STR_CLEARING_CACHE},
    {"CACHE_CLEARED", StrId::STR_CACHE_CLEARED},
    {"NO_CACHE_TO_CLEAR", StrId::STR_NO_CACHE_TO_CLEAR},
    {"CLEARING_STORAGE", StrId::STR_CLEARING_STORAGE},
    {"RESETTING_DEVICE", StrId::STR_RESETTING_DEVICE},
    {"DONE_RESTARTING", StrId::STR_DONE_RESTARTING},
    {"SCANNING", StrId::STR_SCANNING},
    {"CONNECTING", StrId::STR_CONNECTING},
    {"GETTING_IP", StrId::STR_GETTING_IP},
    {"CONNECTED", StrId::STR_CONNECTED},
    {"STARTING", StrId::STR_STARTING},
    {"LOADING", StrId::STR_LOADING},
    {"INDEXING", StrId::STR_INDEXING},
    {"OPENING_BOOK", StrId::STR_OPENING_BOOK},
    {"DELETING", StrId::STR_DELETING},
    {"NO_BOOK_OPEN", StrId::STR_NO_BOOK_OPEN},
    {"PRESS_FILE_TO_EXPLORE", StrId::STR_PRESS_FILE_TO_EXPLORE},
    {"NO_BOOKS_FOUND", StrId::STR_NO_BOOKS_FOUND},
    {"END_OF_BOOK", StrId::STR_END_OF_BOOK},
    {"FAILED_TO_LOAD_PAGE", StrId::STR_FAILED_TO_LOAD_PAGE},
    {"CANNOT_DELETE_ACTIVE", StrId::STR_CANNOT_DELETE_ACTIVE},
    {"DELETED", StrId::STR_DELETED},
    {"DELETE_FAILED", StrId::STR_DELETE_FAILED},
    {"SERVER_STOPPED", StrId::STR_SERVER_STOPPED},
    {"RETURNING_TO_LIBRARY", StrId::STR_RETURNING_TO_LIBRARY},
    {"SLEEPING", StrId::STR_SLEEPING},
    {"CONNECT_WIFI", StrId::STR_CONNECT_WIFI},
    {"CREATE_WIFI_HOTSPOT", StrId::STR_CREATE_WIFI_HOTSPOT},
    {"NO_NETWORKS_FOUND", StrId::STR_NO_NETWORKS_FOUND},
    {"PRESS_CONFIRM_SCAN", StrId::STR_PRESS_CONFIRM_SCAN},
    {"DISCONNECTED_RESTART", StrId::STR_DISCONNECTED_RESTART},
    {"WAITING_FOR_CALIBRE", StrId::STR_WAITING_FOR_CALIBRE},
    {"CONNECTING_TO_CALIBRE", StrId::STR_CONNECTING_TO_CALIBRE},
    {"CALIBRE_HELP", StrId::STR_CALIBRE_HELP},
    {"FMT_IP", StrId::STR_FMT_IP},
    {"FMT_RECEIVED_BOOKS", StrId::STR_FMT_RECEIVED_BOOKS},
    {"FMT_PAGE_OF", StrId::STR_FMT_PAGE_OF},
    {"VERSION", StrId::STR_VERSION},
    {"UPTIME", StrId::STR_UPTIME},
    {"BATTERY", StrId::STR_BATTERY},
    {"CHIP", StrId::STR_CHIP},
    {"CPU", StrId::STR_CPU},
    {"FREE_MEMORY", StrId::STR_FREE_MEMORY},
    {"INTERNAL_DISK", StrId::STR_INTERNAL_DISK},
    {"SD_CARD", StrId::STR_SD_CARD},
    {"READY", StrId::STR_READY},
    {"NOT_AVAILABLE", StrId::STR_NOT_AVAILABLE},
    {"BACKSPACE", StrId::STR_BACKSPACE},
    {"SPACE", StrId::STR_SPACE},
    {"ERROR", StrId::STR_ERROR},
    {"PRESS_ANY_BUTTON", StrId::STR_PRESS_ANY_BUTTON},
    {"CONFIRM_DELETE", StrId::STR_CONFIRM_DELETE},
    {"ENTER_TEXT", StrId::STR_ENTER_TEXT},
    {"NO_COVER", StrId::STR_NO_COVER},
    {"PAPYRIX", StrId::STR_PAPYRIX},
    {"ADD", StrId::STR_ADD},
    {"NO_BOOKMARKS", StrId::STR_NO_BOOKMARKS},
    {"BOOKS", StrId::STR_BOOKS},
    {"DELETE_FILE_Q", StrId::STR_DELETE_FILE_Q},
    {"DELETE_FOLDER_Q", StrId::STR_DELETE_FOLDER_Q},
    {"ENTER_PASSWORD", StrId::STR_ENTER_PASSWORD},
    {"SAVE_PASSWORD_Q", StrId::STR_SAVE_PASSWORD_Q},
    {"SAVE_PASSWORD_MSG", StrId::STR_SAVE_PASSWORD_MSG},
    {"CONNECTION_FAILED", StrId::STR_CONNECTION_FAILED},
    {"HOTSPOT_FAILED", StrId::STR_HOTSPOT_FAILED},
    {"INITIALIZING_WIFI", StrId::STR_INITIALIZING_WIFI},
    {"INVALID_FILE", StrId::STR_INVALID_FILE},
    {"MEMORY_ERROR", StrId::STR_MEMORY_ERROR},
    {"PAGE_LOAD_ERROR", StrId::STR_PAGE_LOAD_ERROR},
    {"RECEIVING", StrId::STR_RECEIVING},
    {"BOOTING", StrId::STR_BOOTING},
};
// clang-format on

static constexpr int KEY_MAP_SIZE = sizeof(KEY_MAP) / sizeof(KEY_MAP[0]);

static_assert(KEY_MAP_SIZE == static_cast<int>(StrId::STR__COUNT), "KEY_MAP size must match StrId::STR__COUNT");

}  // namespace

I18n& I18n::instance() {
  static I18n inst;
  return inst;
}

I18n::I18n() { resetToDefaults(); }

void I18n::resetToDefaults() {
  for (int i = 0; i < static_cast<int>(StrId::STR__COUNT); i++) {
    strings_[i] = i18n::DEFAULTS[i];
  }
  bufferUsed_ = 0;
}

bool I18n::setOverride(const char* keyName, const char* value) {
  if (!keyName || !value || keyName[0] == '_') return false;

  StrId id = StrId::STR__COUNT;
  for (int i = 0; i < KEY_MAP_SIZE; i++) {
    if (strcmp(KEY_MAP[i].name, keyName) == 0) {
      id = KEY_MAP[i].id;
      break;
    }
  }
  if (id == StrId::STR__COUNT) return false;

  const int len = static_cast<int>(strlen(value));
  if (bufferUsed_ + len + 1 > BUFFER_SIZE) return false;

  char* dest = buffer_ + bufferUsed_;
  memcpy(dest, value, len + 1);
  strings_[static_cast<uint8_t>(id)] = dest;
  bufferUsed_ += len + 1;
  return true;
}
