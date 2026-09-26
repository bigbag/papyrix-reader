# PapyriX User Guide

This guide describes controls, navigation, and reading features for supported
PapyriX devices.

## 1. Hardware Overview

Use the firmware file for the device:

- X3/X4: `papyrix-xteink-c3.bin`
- X4 Pro: `papyrix-x4pro.bin`
- X4 v2 Classic: `papyrix-x4c.bin`

X3, the original X4, and X4 v2 Classic use physical buttons.
Only X4 Pro has touch input and a front light.
Pro and Classic firmware images are not interchangeable.

### Button and Touch Layout

X3/X4 bottom buttons are Back, Confirm, Left, and Right. The side controls are
Power, Up, and Down.
X4 v2 Classic has the same seven button actions.
X4 Pro has three physical buttons: Up (GPIO0), Down (GPIO7), and Power (GPIO3).
It has no physical Back, Confirm, Left, or Right buttons.
GT911 touch supplies the other controls.
The capacitive Home key sends Back when released.

On touch devices, reader taps use these zones:

- Left 24 percent: previous page
- Center 52 percent: reader menu
- Right 24 percent: next page

The side-button preference can reverse the page zones. Menus, lists, dialogs,
the keyboard, and reader overlays accept direct taps. Swipe gestures are not
supported.

---

## 2. Power & Startup

### Power On / Off

To start or stop the device, **press and hold the Power button for half a second**. In **Settings** you can set the power button to start on a short press, not a long press.

### First Launch

When you start the device the first time, you see the **Home** screen.

> **Note:** On later restarts, the firmware opens the last book that you read (you can set this with **Startup Behavior** in Settings).

---

## 3. Screens

### 3.1 Home Screen

With cover:

![Home Screen: With cover](images/device.jpg)

Without cover:

![Home Screen: Without Cover](images/home-no-cover.jpg)

Empty:

![Home Screen: Empty](images/home-screen.jpg)


The Home Screen shows the title "PapyriX" at the top with a **battery indicator** in the top-right corner.

#### Book Display
The center of the screen shows the cover of the book that is open. The book title and author are below it.
- **No book open:** Shows "No book open"

#### Bottom Bar
Four buttons at the bottom of the screen:
- **Read** — Continue reading the current book
- **Books** — Open the Books screen (books that you opened before, with access to the file browser)
- **Apps** — Launcher for file transfer (WiFi/Calibre sync), the printer, and other apps
- **Settings** — Device settings

**Navigation:**
* Use **Left/Right** or **Volume Up/Down** to move between buttons
* Press **Confirm** to select

### 3.2 Books (Recent) & File Selection

From the Home screen, press **Books** to open the Books screen.

#### Books (Recent)

The Books screen keeps a maximum of ten books that you opened before. It shows as many as fit on one screen. It orders them with the most recent first. Each row shows the title and author plus reading progress and collected reading time when this data is available.

- **Back:** Go back to the Home screen.
- **Open / Confirm:** Continue the selected book at its saved reading position.
- **Files / Left:** Open the file browser (below).
- **Info / Right:** Open Book Stats for the selected book. This shows Progress, Time read, and Sessions.
- Missing files are removed from the list. There is no Remove action for each book.
- After you read a book that you opened from here, you go back to the Books screen.

#### File Browser

![File Browser](images/file-browser.jpg)

The Files screen is a folder and file browser.

* **Navigate List:** Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to move the selection cursor up
  and down through folders and books.
* **Open Selection:** Press **Confirm** to open a folder or read a selected book.
* **Delete Item:** Press **Right** on an item and confirm the action. Folders are always deleted permanently with all their contents. Files move to the recycle bin (`/trash`) by default. Set **Recycle bin** to off in **Settings → Device** to delete files permanently.

> **Note:** The recycle bin is a regular folder named `trash` at the root of the SD card. It copies the initial folder structure of trashed books. Open its folders and press **Confirm** on a book to put it back in its initial folder (or the root if that folder cannot be created again). Press **Right** to delete it permanently. You cannot delete the `trash` folder from the file browser. Use **Empty Trash** in the Cleanup menu to clear it.

> **Note:** EPUB (.epub), FB2 (.fb2), HTML (.html, .htm), XTC (.xtc, .xtch), Markdown (.md, .markdown), and plain text (.txt, .text) file formats are supported. EPUB 2 and EPUB 3 formats are fully supported. FB2 files support metadata, TOC navigation, and text formatting (no inline images). HTML files show as standalone documents with formatting. Markdown files show with basic formatting (headers, bold, italic, lists). The device supports SD cards with FAT32 format and exFAT format.

> **Tip:** The Web UI supports folder names and file names in Latin (including Vietnamese), Cyrillic, Greek, Thai, and Arabic. Names have a limit of 255 UTF-8 bytes. Full paths have a limit of 1023 bytes. CJK filenames are not supported in the device file browser. For deep folder trees with supported non-Latin names, use exFAT. Make a backup of the SD card before you format it again.

> **Note:** These folders are hidden from the file browser:
> - `System Volume Information`, `LOST.DIR`, `$RECYCLE.BIN` — OS system folders
> - `config` — PapyriX configuration files
> - `XTCache` — XTC format cache
> - `sleep` — Custom sleep screen images
> - `.papyrix` — Internal cache (dot-prefix hidden by default)

> **Note:** Each folder can show a maximum of 1000 items. Put large libraries into subfolders if you go above this limit.

> **Note:** You cannot delete the book that is open. Close the book first, then delete it.

### 3.3 Reading Screen

Text:

![Reading View: Text](images/reading-text.jpg)

Images:

![Reading View: Image](images/reading-image.jpg)

Arabic (RTL):

![Reading View: Arabic](images/reading-arabic.jpg)

Landscape:

![Reading View: Landscape](images/reading-landscape.jpg)

See [4. Reading Mode](#4-reading-mode) below for more data.

### 3.4 File Transfer (Sync)

You get file transfer from the Home screen. Open **Apps** and select **File Transfer**. This lets you upload new e-books to the device through WiFi or connect to Calibre.

When you go into the screen, the device asks you to select a network mode:

* **Recent:** Try saved networks in their saved order.
* **Join Network:** Scan for available WiFi networks. Enter a password when required. The device uses a saved password if one exists.
* **Create Hotspot:** The device makes its own WiFi network. You can connect to it from your computer or phone.

![On-screen Keyboard](images/keyboard.jpg)

After the connection, your X4 starts a web server. See the [webserver docs](webserver.md) for
how to connect and upload files.

> **Note:** When you exit File Transfer, the device restarts to get memory back that WiFi used.

### 3.5 Settings

The Settings screen has six categories in the same order on all supported devices.
Reader, Screen, and Device are separate entries on the same level.
Missing translations use the English fallback. Use the
[locale examples](examples/locale/) for translated labels.

#### Reader

Text and reading settings, in menu order:

- **Font Size** (default: Small)
  - Options: XSmall (12pt), Small (14pt), Normal (16pt), Large (18pt)
  - Text size for reading

- **Text Layout** (default: Standard)
  - Options: Compact, Standard, Large
  - Controls first-line indent and paragraph spacing:
    - **Compact:** No indent, no extra spacing (dense text)
    - **Standard:** Usual indent (em-space), small spacing between paragraphs
    - **Large:** Large indent, full line spacing between paragraphs

- **Line Spacing** (default: Normal)
  - Options: Compact, Normal, Relaxed, Large
  - Controls vertical spacing between lines in paragraphs:
    - **Compact:** Tighter line spacing (0.85×)
    - **Normal:** Standard line spacing (0.95×)
    - **Relaxed:** More space between lines (1.10×)
    - **Large:** Maximum line spacing (1.20×)
  - A change of line spacing can increase readability for different fonts and preferences

- **Paragraph Alignment** (default: Justified)
  - Options: Justified, Left, Center, Right
  - Text alignment for paragraphs (headers stay centered)

- **Hyphenation** (default: ON)
  - Break long words at soft hyphen positions in EPUB content
  - Words that are too wide for the line are split with character-level hyphenation
  - Decreases large gaps in justified text and prevents words from going past the line

- **Show Images** (default: ON)
  - Show inline images in EPUB content and book covers
  - Set to off for faster page rendering (images show an "[Image]" placeholder)

- **Status Bar** (default: Title)
  - Options: None, Title, Chapter, Filename.
  - **Title:** Shows the book title, battery, and page number.
  - **Chapter:** Shows the current chapter when available. Otherwise, it shows the book title.
  - **Filename:** Shows the file name on the SD card, including its extension.
  - **None:** Hides the status bar to give more space for book text.
  - Long titles and file names are shortened to fit the status bar.
  - The page indicator marks estimated totals with `~`. See [Status Bar](#status-bar).

- **Touch page turns** (X4 Pro only, default: ON)
  - Enable or disable taps in the previous-page and next-page zones.
  - Center menu taps and overlay controls stay active.
  - X3, X4, and X4 v2 Classic hide this setting.
  - Physical buttons stay active.

- **Full Book Process** (default: OFF)
  - When this is on, the device indexes all pages of the book before you start reading
  - Shows a progress bar during indexing. Press **Back** to cancel and go back to the file list
  - After indexing, the exact total page count is immediately available in the status bar
  - Useful for books where you want accurate page counts from the start (skipped for XTC/XTCH files)
  - Sections that are already cached are skipped, so a book that you indexed before opens immediately



#### Screen

Display settings, in menu order:

- **Theme** (default: light)
  - Select from available themes (light, dark, or custom themes from the SD card)
  - Themes control colors, layout, and fonts
  - See [Customization Guide](customization.md) to make custom themes

- **Brightness** (X4 Pro only)
  - Adjust front-light brightness from 0 to 100 percent in steps of 5.
  - Zero turns the front light off.

- **Warmth** (X4 Pro only)
  - Adjust the cool/warm light mix from 0 to 100 percent in steps of 5.
  - X3, X4, and X4 v2 Classic hide both front-light settings.

- **Reading Orientation** (default: Portrait)
  - Options: Portrait, Landscape CW, Inverted, Landscape CCW
  - Screen orientation for reading

- **Text Anti-Aliasing** (default: OFF)
  - Set grayscale text rendering to on for smoother font edges
  - Operates with builtin fonts and custom fonts converted with `--2bit`
  - Set to off for faster page turns and to remove the short "thick text" flash during transitions

- **Pages Per Refresh** (default: 15)
  - Options: 1, 5, 10, 15, 30
  - Number of pages to turn before a full e-paper refresh (clears ghosting)

- **Sunlight Fading Fix** (default: OFF)
  - Powers down the display after each page refresh
  - Prevents screen fade in bright sunlight (UV exposure causes the SSD1677 driver IC to fade to white)
  - Adds approximately 100-200ms overhead for each page turn
  - Recommended for white X4 devices that you use outdoors

- **Sleep Screen** (default: Dark)
  - Options: Dark, Light, Custom, Cover, Keep Page
  - Which image to show when the device sleeps

#### Device

Controls and device behavior, in menu order:

- **WiFi**
  - Open **Saved networks** to add or edit an SSID and password without a connection. You can save up to eight networks.
  - Select a saved network to connect, edit, or forget it. Press **Left** or **Right** on the saved list to move it up or down.
  - **Recent** tries saved networks in the list order. A successful connection moves that network to the first position. You can change the order again.
  - **Join Network** scans for a network. **Create Hotspot** starts a WiFi access point.
  - A connection starts the file-transfer server. Leave the screen to stop WiFi.

- **Front Buttons** (default: B/C/L/R)
  - Options: B/C/L/R, L/R/B/C
  - **B/C/L/R:** Back, Confirm, Left, Right (default layout)
  - **L/R/B/C:** Left, Right, Back, Confirm (changed layout)

- **Side Buttons** (default: Prev/Next)
  - Options: Prev/Next, Next/Prev
  - **Prev/Next:** Volume Up = previous page, Volume Down = next page
  - **Next/Prev:** Volume Up = next page, Volume Down = previous page

- **Short Power Button** (default: Ignore)
  - Options: Ignore, Sleep, Page Turn, Bookmark
  - **Ignore:** Short press does nothing (long press for sleep)
  - **Sleep:** Short press puts the device to sleep
  - **Page Turn:** Short press goes to the next page while you read (useful for one-handed reading)
  - **Bookmark:** Short press bookmarks the current page while you read (shows a short notification)


- **Startup Behavior** (default: Last Document)
  - Options: Last Document, Home
  - **Last Document:** Continue the last opened book on start
  - **Home:** Always start at the Home screen

- **Show Recents** (default: ON)
  - Options: OFF, ON
  - **ON:** The Home screen shows a **Books** button that opens the books that you opened before (with a **Files** button to browse the SD card).
  - **OFF:** The Home screen shows a **Files** button that opens the file browser. The device still records opened books. Enable this setting to show the history again. Book Stats remains available from the Reader Menu.

- **Auto Sleep Timeout** (default: 10 min)
  - Options: 5 min, 10 min, 15 min, 30 min, Never
  - Time with no activity before the device sleeps

- **Recycle bin** (default: ON)
  - Options: OFF, ON
  - **ON:** If you delete a file from the Files screen, the device moves it to `/trash`. You can restore it or delete it permanently.
  - **OFF:** If you delete a file from the Files screen, the device removes it permanently. Recovery is not available.
  - Folders are always deleted permanently with all their contents, for all values of this setting.

#### Cleanup

Maintenance actions for the device:

- **Clear Book Cache** — Delete all cached book data and reading progress
- **Clear recent books** — Clear the Books history only. Book files, reading progress, bookmarks, caches, and reading statistics stay.
- **Empty Trash** — Permanently delete all contents of the recycle bin (`/trash`)
- **Clear Device Storage** — Erase internal flash storage (needs a restart)
- **Factory Reset** — Erase ALL data (caches, settings, WiFi credentials, fonts) and restart the device

#### Firmware Update

Install firmware updates from an SD card:

- Copy the firmware binary as `/firmware.bin` to the root of your SD card
- Select **Firmware Update** from the Settings menu and press **Run**
- The device flashes the firmware and restarts
- **Do not power off or remove the SD card during the update**

For emergency recovery, copy the firmware as `/force_update.bin` to the SD card.
On the next start, the device shows **Firmware update in progress** and **Do not
power off**, applies the update, and restarts. If the display is unavailable,
the update continues without changing the previous e-ink frame. Do not remove
power or the SD card before the restart.


You can also upload firmware binaries through the [web server](webserver.md). Use the **Firmware** tab to upload a `.bin` file, then run the update from the device.

#### System Info

See device data: firmware version, uptime, WiFi status, MAC address, free memory, internal disk use, SD card use

### 3.6 Calibre Wireless

Calibre Wireless lets you send books from **Calibre** (ebook management software) to your PapyriX Reader through WiFi. This is the fastest method to send books if you already use Calibre.

#### Prerequisites

- [Calibre](https://calibre-ebook.com/) installed on your computer
- The two devices on the same WiFi network

#### Connecting to Calibre

1. From the Home screen, open **Apps** and select **Calibre Wireless**
2. Connect to your WiFi network (the same as your computer)
3. The device shows its IP address and port (for example, `192.168.1.42:9090`)
4. The screen shows "Waiting for Calibre..."

#### In Calibre Desktop

1. Click **Connect/Share** in the toolbar
2. Select **Start wireless device connection**
3. Calibre finds your PapyriX Reader
4. Your device shows as "PapyriX Reader" (or your custom name)

#### Sending Books

After the connection:
1. Right-click a book in Calibre
2. Select **Send to device > Send to main memory**
3. The book transfers through WiFi to the `/Books/` folder of your reader

#### Bidirectional Sync

Calibre can also:
- **See your library** - See books that are already on your device
- **Delete books** - Remove books from the device from Calibre

#### Configuration

Change settings through `/config/calibre.ini` on your SD card:

```ini
[Settings]
device_name = PapyriX Reader
password =
```

- **device_name**: How your device shows in Calibre
- **password**: Optional password (must match the Calibre wireless device password)

For the full procedure, see the [Calibre Wireless Guide](calibre.md).

> **Note:** When you exit Calibre Wireless, the device restarts to get memory back that WiFi used.

### 3.7 Sleep Screen

![Sleep Screen](images/sleep-screen.jpg)

You can change the sleep screen. Put custom images in specified locations on the SD card:

- **Single Image:** Put a file named `sleep.bmp` in the root directory.
- **Multiple Images:** Make a `sleep` directory in the root of the SD card and put `.bmp` images
  in it. If images are in this directory, they have priority over the `sleep.bmp` file. One is
  selected randomly each time the device sleeps.

> [!NOTE]
> You must set the **Sleep Screen** setting to **Custom** to use these images.

#### Image Parameters

- **Resolution:** 480 × 800 pixels for X4, 528 × 792 pixels for X3 (portrait mode)
- **Color depth:** 8-bit grayscale or 24-bit color
- **Format:** BMP, uncompressed (BI_RGB)
- **Display levels:** 4 grayscale (black, dark gray, light gray, white)

> [!TIP]
> - Use 8-bit grayscale images.
> - Larger images are scaled down. Aspect ratio stays the same.
> - All color images are converted to 4-level grayscale on the e-ink display.

> [!TIP]
> The **Cover** sleep screen option shows the cover of the book that is open when the device sleeps.

Cover mode:

![Sleep Screen: Cover](images/sleep-screen-cover.jpg)

> [!TIP]
> The **Keep Page** sleep screen option keeps the current book page visible while the device sleeps. It does not show a sleep screen. It is only available while you read. If you are not in a book, it uses the Light sleep screen.

### 3.8 Clock

Open **Apps → Clock**. If the device has valid time, Clock opens without a WiFi connection.
If time is not set, Clock opens WiFi setup. Connect to a network to synchronize the time.
Press **Back** in WiFi setup to return to Apps without synchronization.

Open **Menu** to change the clock face, time zone, time format, date format, or NTP interval.
The available faces are **Big**, **Analog**, **Retro**, **Flip**, and **Day & Night**.
Each face shows the date at the top and centers the clock in the usable screen area.
**Day & Night** shows the sun below the time from 06:00 through 17:59 local time.
At night, it shows the current lunar phase below the time and the phase name below the moon.
New installations use **Big**. Existing Clock settings keep **Retro** until you select another face.
Settings use a full-screen list. Each setting shows its current value on the right.
The settings menu does not show the clock face or battery indicator.
Press **Right** to select the next value. Press **Left** to select the previous value.
Press **Back** to apply and save changes. Saving settings does not request synchronization.

Select **Sync Now** and press **Right** to synchronize immediately, even when the NTP interval is **Off**.
The right button shows **Sync** on this row. Other settings show `<` and `>`.
If no WiFi network is saved, Clock opens WiFi setup. Connect to a network to complete the synchronization.

Automatic synchronization pauses while the settings menu is open. An overdue attempt can run after you close the menu.
Clock skips automatic synchronization when no WiFi network is saved.
The **Off** NTP interval disables periodic synchronization.

The settings menu keeps panel power on between updates.
The CPU stays at full speed while the settings menu is open.
Clock turns off panel power after each update of the clock face.
Clock also turns off panel power after synchronization status messages, before it waits for WiFi or NTP.
The image remains visible. This does not change the front-light brightness.

### 3.9 Printer

Open **Apps → Printer**. The device becomes a driverless network printer.

If a WiFi network is saved, the app connects without asking.
If no network is saved, or every saved network fails, the app opens the WiFi
picker. The picker shows saved networks, a scan for new networks, and the
hotspot option. The hotspot starts a WiFi access point named **PapyriX**.

After the app connects, the waiting screen shows the network name, the IP
address, and the printer URI. Print from a computer or phone on the same
network:

- **macOS**: Select **PapyriX** in the print dialog. You do not need a driver.
- **Windows 10/11**: Add the printer in **Settings → Printers & scanners**.
- **Linux**: The printer appears in CUPS. Add it manually with
  `lpadmin -p PapyriX -E -v ipp://<device-ip>:631/ipp/print -m everywhere`.
- **iPhone/iPad**: The printer appears in the print share sheet.
- **Android**: Use a print service that supports IPP Everywhere, for example the Mopria Print Service.

Each printed page saves to the `/printouts` directory on the SD card.
Browse earlier printouts with **Left** and **Right** on the waiting screen.
Printouts also appear in the **Image Viewer** app.
One page prints per job. The maximum job size is 8 MB.

Press **Back** to stop the printer and shut the WiFi down.

### 3.10 LocalSend

Open **Apps → LocalSend**. The device becomes a LocalSend receiver.

The WiFi picker behaves like the printer app. The waiting screen shows the
network name and the IP address.

Send files or books from the LocalSend app on your computer or phone:

- Turn **Encryption** off in the LocalSend settings on the sender. The device
  uses plain HTTP.
- Select the device named **PapyriX** in the LocalSend app and send.

Received files save to the `/received` directory on the SD card. If a file
with the same name exists, the new file gets a number suffix.

Press **Back** to stop the receiver and shut the WiFi down.

---

## 4. Reading Mode

After you open a book, the button layout changes to help you read.

### Page Turning

- **Previous Page:** Press **Left** or **Volume Up**
- **Next Page:** Press **Right** or **Volume Down**
- **Power Button:** When **Short Power Button** is set to **Page Turn** in Settings, a press of the power button goes to the next page (useful for one-handed reading). When set to **Bookmark**, it bookmarks the current page.

### Chapter Navigation
* **Next Chapter:** Press and **hold** the **Right** (or **Volume Down**) button for a short time, then release.
* **Previous Chapter:** Press and **hold** the **Left** (or **Volume Up**) button for a short time, then release.

### System Navigation
* **Return to Home:** Press **Back** to close the book and go back to the Book Selection screen.
* **Reader Menu:** Press **Confirm** to open the Reader Menu (access chapters, bookmarks, and Book Stats).

### Status Bar

When **Settings → Reader → Status Bar** is not **None**, the bottom of the reading screen shows the battery, the selected title, chapter, or file name, and the page indicator on the right. The page indicator has three forms:

- **`123/456`** — exact total. The full book is laid out and cached.
- **`123/456~`** — the total is an estimate. The cache is still built in increments (the number increases as you read) or — for non-EPUB formats with no cache yet (for example, immediately after **Clear Book Cache**) — it is a file-size estimate. The number changes to the exact total when cache completes.
- **`123/-`** — unknown. Content is still loading. Temporary.

EPUB chapters cache one chapter at a time, so `~` usually clears when the current chapter cache completes. TXT / Markdown / FB2 / HTML cache the full book in chunks, so `~` can stay until you read through (or go past) the full book.

### 4.1 Reader Menu

Press **Confirm** while you read to open the Reader Menu. The menu is not available on the cover page. Turn to the first text page first. The menu has three options:

- **Chapters** — Open the Table of Contents / Chapter Selection screen
- **Bookmarks** — Open the Bookmarks overlay for the current book
- **Book stats** — Show Progress, total Time read, and Sessions for the current book

Book Stats stays available here when **Show Recents** is off. Use **Left/Right** to highlight a menu item and **Confirm** to select. Press **Back** to close the menu and go back to reading.

### 4.2 Bookmarks

The Bookmarks overlay lists all saved bookmarks for the current book, sorted by page position.

#### Controls

- **Back** — Close the bookmarks overlay
- **Go** — Go to the page of the selected bookmark
- **Add** — Bookmark the current page
- **Del** — Remove the selected bookmark

Use **Left/Right** (or **Volume Up/Down**) to move the selection cursor through the bookmark list.

#### Details

- Each book can have a maximum of **50 bookmarks**. The device does not add a bookmark when the book has 50 bookmarks. It does not show a message.
- Duplicate bookmarks at the same position are not permitted.
- Bookmark labels include the chapter title and page number (for example, "Chapter 1, p.42").
- You can also add bookmarks with the **Power button** when **Short Power Button** is set to **Bookmark** in Settings.
- Bookmarks are **saved to the SD card** and stay after device restarts.
- Each book has its own set of bookmarks.

---

## 5. Chapter Selection Screen

![Table of Contents](images/table-of-contents.jpg)

Arabic (RTL):

![Table of Contents: Arabic](images/table-of-contents-arabic.jpg)

Available from the Reader Menu if you select **Chapters**. The screen header shows the book title.

1.  Use **Up** or **Down** (Volume Up / Volume Down) to highlight the chapter that you want.
2.  Use **Left** or **Right** to page up or page down through the list.
3.  Press **Confirm** to go to the selected chapter.
4.  *Or press **Back** to cancel and go back to your current page.*

---

## 6. Unsupported Content

* **Tables:** The reader shows a `[Table omitted]` placeholder for HTML tables.
* **Image formats:** EPUB supports JPEG, PNG, and BMP images. GIF, SVG, and WebP images show a placeholder.

---

## 7. Customization

For the full procedure to make custom themes and add custom fonts, see the [Customization Guide](customization.md).
