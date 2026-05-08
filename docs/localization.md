# Localization

Papyrix includes English as the default language compiled into firmware. A different language can be used by placing a single translation file on the SD card or uploading it via the web interface.

## How it works

1. English strings (179 keys) are compiled into firmware (Flash, zero RAM cost)
2. At boot, if `/.papyrix/locale.txt` exists on SD, it overrides English defaults in RAM
3. All `tr()` calls resolve to a single pointer dereference - no SD access after boot
4. To change language, replace the file and restart

## File format

Place a locale file at `/.papyrix/locale.txt` on the SD card.

```
# Comment lines start with #
_language_name=Francais

BACK=Retour
OPEN=Ouvrir
SETTINGS=Parametres
LOADING=Chargement...
```

- Keys match the StrId enum names exactly (case-sensitive)
- Missing keys fall back to compiled English
- Unknown keys are silently ignored
- Keys starting with `_` are reserved for metadata (see below)
- Comments: lines starting with `#` or `;`
- UTF-8 encoding
- Max line length: 255 characters (longer lines are truncated)
- Override buffer: 4096 bytes total for all translated strings combined

### Metadata keys

Keys starting with `_` are not loaded into the translation table but may be used by the system:

- `_language_name` - displayed in the web interface as the current language name

## Managing translations

### Via SD card

1. Copy one of the examples from `docs/examples/locale/` to `/.papyrix/locale.txt` on your SD card
2. Replace values with your translations
3. Restart the device

### Via web interface

1. Connect to the device via WiFi (Join Network or Create Hotspot)
2. Open the web interface and go to the **Locale** tab
3. Upload a `.txt` locale file
4. Restart the device

The web interface shows the current language name (from `_language_name`) and file size, and allows deleting the locale file to revert to English.

See `docs/examples/locale/` for complete examples (en, de, fr, es, uk).

## String keys reference

See `lib/I18n/src/I18nDefaults.h` for the complete list of all string keys and their English defaults.

Format strings (keys starting with `FMT_`) contain `%s` or `%d` placeholders that must be preserved in translations:

- `FMT_IP` - `IP: %s` (%s = IP address)
- `FMT_RECEIVED_BOOKS` - `Received %d book(s)` (%d = book count)
- `FMT_PAGE_OF` - `of %d` (%d = total pages)
