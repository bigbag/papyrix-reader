# Localization

Papyrix includes English as the default language. English is compiled into the firmware. You can use a different language. Put one translation file on the SD card, or upload it through the web interface.

## How it works

1. English strings (217 keys) are compiled into firmware (Flash, zero RAM cost).
2. At start, if `/.papyrix/locale.txt` is on the SD card, it replaces English defaults in RAM.
3. All `tr()` calls go to one pointer dereference. There is no SD access after start.
4. To change the language, replace the file and start the device again.

## File format

Put a locale file at `/.papyrix/locale.txt` on the SD card.

```
# Comment lines start with #
_language_name=Francais

BACK=Retour
OPEN=Ouvrir
SETTINGS=Parametres
LOADING=Chargement...
```

- Keys match the StrId enum names exactly (case-sensitive).
- Missing keys use the compiled English text.
- Unknown keys are ignored with no message.
- Keys that start with `_` are reserved for metadata (see below).
- Comments: lines that start with `#` or `;`.
- UTF-8 encoding.
- Maximum line length: 255 characters (longer lines are cut).
- Override buffer: 4096 bytes total for all translated strings together.
- Complete example locales must keep the UTF-8 byte total of all non-metadata values at 4096 bytes or less. Values that go above the limit keep their English defaults with no message.

### Metadata keys

Keys that start with `_` are not loaded into the translation table. The system can use them:

- `_language_name` - shown in the web interface as the current language name

## Managing translations

### From the SD card

1. Copy one of the examples from `docs/examples/locale/` to `/.papyrix/locale.txt` on your SD card.
2. Replace values with your translations.
3. Start the device again.

### From the web interface

1. Connect to the device through WiFi (Join Network or Create Hotspot).
2. Open the web interface and go to the **Locale** tab.
3. Upload a `.txt` locale file.
4. Start the device again.

The web interface shows the current language name (from `_language_name`) and the file size. You can delete the locale file to go back to English.

See `docs/examples/locale/` for complete examples (en, de, fr, es, uk).

## String keys reference

See `lib/I18n/src/I18nDefaults.h` for the full list of all string keys and their English defaults.

Format strings (keys that start with `FMT_`) contain `%s` or `%d` placeholders. You must keep these placeholders in translations:

- `FMT_IP` - `IP: %s` (%s = IP address)
- `FMT_RECEIVED_BOOKS` - `Received %d book(s)` (%d = book count)
- `FMT_PAGE_OF` - `of %d` (%d = total pages)
