#include "I18nLoader.h"

#include <I18n.h>
#include <Logging.h>
#include <SDCardManager.h>

#include <cstring>

#include "IniParser.h"
#include "config.h"

#define TAG "I18N"

static constexpr const char* LOCALE_FILE = PAPYRIX_DIR "/locale.txt";

namespace i18n {

void loadLocaleFromSD() {
  if (!SdMan.exists(LOCALE_FILE)) return;

  int count = 0;
  IniParser::parseFile(LOCALE_FILE, [&count](const char*, const char* key, const char* value) -> bool {
    if (I18n::instance().setOverride(key, value)) {
      count++;
    }
    return true;
  });

  LOG_INF(TAG, "Loaded locale: %d strings, %d/%d bytes", count, I18n::instance().bufferUsed(), I18n::BUFFER_SIZE);
}

}  // namespace i18n
