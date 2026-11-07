# Localization

PapyriX uses English by default. To use another language, put a translation file on the SD card. The web interface also accepts translation uploads.

## Operation

- The firmware stores English strings in Flash.
- At startup, `/.papyrix/locale.txt` supplies replacement strings in RAM.
- String lookup does not access the SD card after startup.

Start the device again after replacing the locale file.

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
- The loader ignores unknown keys without a message.
- The loader reserves keys that start with `_` for metadata.
- Comments: lines that start with `#` or `;`.
- UTF-8 encoding.
- Maximum line length: 255 UTF-8 bytes for the full `KEY=value` line, without its line ending. The loader cuts longer lines.
- Override buffer: 4608 bytes for all translated strings.
- Count the UTF-8 bytes of all non-metadata values and one terminating null byte per value. Keep this total at 4608 bytes or less. Values that do not fit keep their English defaults without a message.
- Standard button-bar labels have 94 pixels for text. Their width depends on the font, not only the character count. Check the rendered labels.

### Metadata keys

The translation table excludes keys that start with `_`.

- `_language_name` - The web interface shows this value as the current language name.

## Managing translations

### From the SD card

1. Copy one of the examples from `docs/examples/locale/` to `/.papyrix/locale.txt` on your SD card.
2. Replace values with your translations.
3. Start the device again.

### From the web interface

1. Connect to the device through WiFi (Join Network or Create Hotspot).
2. Open the web interface.
3. Select the **Locale** tab.
4. Upload a `.txt` locale file.
5. Start the device again.

The web interface shows the language name and file size. Delete the locale file to restore English.

See `docs/examples/locale/` for examples (en, de, fr, es, uk).
The English, German, Spanish, and French examples include every key.
The Ukrainian example omits 19 `GO_*` keys to stay within the override buffer.
These keys use the compiled English text.

Run `make test-tools` to check the examples.
The validator requires every key in the English example.
Other examples can omit `GO_*` keys.
It rejects unknown keys and data above the override buffer limit.
It also requires different labels for the two landscape orientations.
It does not check the 255-byte line limit or rendered label width.

## String keys reference

See `lib/I18n/src/I18nDefaults.h` for the full list of all string keys and their English defaults.

Format strings (keys that start with `FMT_`) contain `%s` or `%d` placeholders. You must keep these placeholders in translations:

- `FMT_IP` - `IP: %s` (%s = IP address)
- `FMT_RECEIVED_BOOKS` - `Received %d book(s)` (%d = book count)
- `FMT_PAGE_OF` - `of %d` (%d = total pages)
