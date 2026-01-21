# Papyrix User Guide

Welcome to the **Papyrix** firmware. This guide outlines the hardware controls, navigation, and reading features of
the device.

## 1. Hardware Overview

The device utilises the standard buttons on the Xtink X4 in the same layout:

### Button Layout

- **Bottom Edge:** Back, Confirm, Left, Right
- **Right Side:** Power, Volume Up, Volume Down

---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for half a second**. In **Settings** you can configure
the power button to trigger on a short press instead of a long one.

### First Launch

Upon turning the device on for the first time, you will be placed on the **Home** screen.

> **Note:** On subsequent restarts, the firmware will automatically reopen the last book you were reading (configurable via **Startup Behavior** in Settings).

---

## 3. Screens

### 3.1 Home Screen

![Home Screen](images/home-screen.jpg)

The Home Screen displays the app title "Papyrix Reader" at the top with a **battery indicator** in the top-right corner.

#### Book Card
The center of the screen features a **book card** - a dark rectangle with a bookmark icon at the top:
- **With a book open:** Displays the book title, author, and "Continue Reading" text
- **No book open:** Displays "No book open"

Press **Confirm** on the book card to continue reading your current book.

#### Menu Items
Below the book card are two bordered menu items:
- **Files** — Browse and select books from the SD card
- **Settings** — Device settings and file transfer

**Navigation:**
* Use **Left/Right** or **Volume Up/Down** to move between items
* Press **Confirm** to select an item

### 3.2 Book Selection (Files)

![File Browser](images/file-browser.jpg)

The Files screen acts as a folder and file browser.

* **Navigate List:** Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to move the selection cursor up
  and down through folders and books.
* **Open Selection:** Press **Confirm** to open a folder or read a selected book.

> **Note:** EPUB (.epub), XTC (.xtc, .xtch), Markdown (.md, .markdown), and plain text (.txt, .text) file formats are supported. EPUB 2 and EPUB 3 formats are fully supported. Markdown files render with basic formatting (headers, bold, italic, lists). The device supports both FAT32 and exFAT formatted SD cards.

> **Note:** System folders (LOST.DIR, $RECYCLE.BIN, etc.) are automatically hidden from the file browser.

### 3.3 Reading Screen

![Reading View](images/reading-view.jpg)

See [4. Reading Mode](#4-reading-mode) below for more information.

### 3.4 File Transfer

![File Transfer](images/file-transfer.jpg)

File transfer is accessible from **Settings > File transfer**. This allows you to upload new e-books to the device over WiFi.

When you enter the screen, you'll be prompted to choose a network mode:

* **Join Network:** Connect to an existing WiFi network. You'll see a list of available networks and can enter passwords as needed. Previously saved networks will connect automatically.
* **Create Hotspot:** The X4 creates its own WiFi network that you can connect to directly from your computer or phone.

![On-screen Keyboard](images/keyboard.jpg)

Once connected, your X4 will start hosting a web server. See the [webserver docs](webserver.md) for more
information on how to connect and upload files.

> **Note:** When you exit File Transfer, the device will automatically restart to reclaim memory used by WiFi.

### 3.5 Settings

![Settings](images/settings.jpg)

The Settings screen allows you to configure the device's behavior:

- **Theme** (default: light)
  - Select from available themes (light, dark, or custom themes from SD card)
  - Themes control colors, button mappings, and fonts
  - See [Customization Guide](customization.md) for creating custom themes

- **Sleep Screen** (default: Dark)
  - Options: Dark, Light, Custom, Cover
  - Which image to display when the device sleeps

- **Status Bar** (default: Full)
  - Options: None, No Progress, Full
  - Status bar display during reading

- **Font Size** (default: Normal)
  - Options: Small (14pt), Normal (16pt), Large (18pt)
  - Text size for reading

- **Paragraph Alignment** (default: Justified)
  - Options: Justified, Left, Center, Right
  - Text alignment for paragraphs (headers remain centered)

- **Text Layout** (default: Standard)
  - Options: Compact, Standard, Large
  - Controls first-line indentation and paragraph spacing:
    - **Compact:** No indent, no extra spacing (dense text)
    - **Standard:** Normal indent (em-space), small spacing between paragraphs
    - **Large:** Large indent, full line spacing between paragraphs

- **Hyphenation** (default: ON)
  - Break long words at soft hyphen positions embedded in EPUB content
  - Words too wide for the line are automatically split with character-level hyphenation
  - Reduces large gaps in justified text and prevents words from overflowing

- **Text Anti-Aliasing** (default: ON)
  - Enable grayscale text rendering for smoother font edges
  - Disable for faster page turns and to eliminate the brief "thick text" flash during transitions

- **Show Images** (default: ON)
  - Display inline images within EPUB content and book covers
  - Disable for faster page rendering (images show "[Image]" placeholder)

- **Pages Per Refresh** (default: 15)
  - Options: 1, 5, 10, 15, 30
  - Number of pages to turn before performing a full e-paper refresh (clears ghosting)

- **Short Power Button** (default: Ignore)
  - Options: Ignore, Sleep, Page Turn
  - **Ignore:** Short press does nothing (long press for sleep)
  - **Sleep:** Short press puts device to sleep
  - **Page Turn:** Short press turns to next page while reading (convenient for one-handed reading)

- **Reading Orientation** (default: Portrait)
  - Options: Portrait, Landscape CW, Inverted, Landscape CCW
  - Screen orientation for reading

- **Sleep Timeout** (default: 10 min)
  - Options: 5 min, 10 min, 15 min, 30 min, Never
  - Time of inactivity before the device automatically sleeps

- **Startup Behavior** (default: Last Document)
  - Options: Last Document, Home
  - **Last Document:** Resume reading the last opened book on startup
  - **Home:** Always start at the Home screen

- **File transfer**
  - Upload e-books to the device via WiFi web server

- **Check for updates**
  - Check for and install firmware updates via WiFi
  - Device will restart after exiting to reclaim WiFi memory

- **Net Library**
  - Browse and download books from OPDS-compatible servers
  - See [3.6 Net Library](#36-net-library) below

- **Calibre Wireless**
  - Connect directly to Calibre desktop to send/receive books
  - See [3.7 Calibre Wireless](#37-calibre-wireless) below

- **Cleanup**
  - **Clear Book Caches** — Delete all cached book data and reading progress
  - **Clear Installed Font** — Remove custom font from internal flash, revert to builtin (requires restart)
  - **Factory Reset** — Erase ALL data (caches, settings, WiFi credentials, fonts) and restart device

- **System Info**
  - View device information: firmware version, uptime, WiFi status, MAC address, free memory, internal disk usage, SD card usage

### 3.6 Net Library

Net Library allows you to browse and download books directly from any [OPDS](https://opds.io/)-compatible server running on your local network. OPDS (Open Publication Distribution System) is an open standard supported by many ebook servers including:

- **Calibre Content Server** — Built into Calibre
- **COPS** — Calibre OPDS (PHP Server)
- **Kavita** — Self-hosted digital library
- **Komga** — Media server for comics/manga
- **Feedbooks** — Public domain books

#### Getting Started

On first launch, **Project Gutenberg** is preconfigured as a default server, giving you immediate access to over 70,000 free public domain ebooks.

#### Adding More Servers

Servers are configured via the `/config/opds.ini` file on your SD card:

1. Insert the SD card into your computer
2. Open or create `/config/opds.ini`
3. Add servers using the INI format:

```ini
[My Calibre Server]
url = http://192.168.1.100:8080/opds
username = myuser
password = mypassword
```

> **Tip:** For Calibre, enable the content server in Preferences > Sharing over the net, then use `http://your-ip:8080/opds` as the URL.

#### Browsing Books

1. Select a configured server from the list
2. The device will connect via WiFi (you'll be prompted to select a network if not connected)
3. Navigate through catalogs using **Left/Right** or **Volume Up/Down**
4. Press **Confirm** to enter a catalog or download a book
5. Press **Back** to go up one level

#### Supported Formats

Books are downloaded in EPUB format. If a book doesn't have an EPUB version available, it cannot be downloaded.

#### Managing Servers

To add, edit, or remove servers, modify the `/config/opds.ini` file on your SD card.

Downloaded books are saved to the `/Books/` folder on your SD card.

> **Note:** When you exit Net Library, the device will automatically restart to reclaim memory used by WiFi.

### 3.7 Calibre Wireless

Calibre Wireless allows you to send books directly from **Calibre** (the popular ebook management software) to your Papyrix Reader over WiFi. This is the fastest way to transfer books if you already use Calibre.

#### Prerequisites

- [Calibre](https://calibre-ebook.com/) installed on your computer
- Both devices on the same WiFi network

#### Connecting to Calibre

1. Go to **Settings > Calibre Wireless**
2. Connect to your WiFi network (same as your computer)
3. The device will show its IP address and port (e.g., `192.168.1.42:9090`)
4. The screen displays "Waiting for Calibre..."

#### In Calibre Desktop

1. Click **Connect/Share** in the toolbar
2. Select **Start wireless device connection**
3. Calibre will automatically discover your Papyrix Reader
4. Your device appears as "Papyrix Reader" (or your custom name)

#### Sending Books

Once connected:
1. Right-click any book in Calibre
2. Select **Send to device > Send to main memory**
3. The book transfers wirelessly to your reader's `/Books/` folder

#### Bidirectional Sync

Calibre can also:
- **See your library** - View books already on your device
- **Delete books** - Remove books from the device remotely

#### Configuration

Customize settings via `/config/calibre.ini` on your SD card:

```ini
[Settings]
device_name = Papyrix Reader
password =
```

- **device_name**: How your device appears in Calibre
- **password**: Optional password (must match Calibre's wireless device password)

For detailed instructions, see the [Calibre Wireless Guide](calibre.md).

> **Note:** When you exit Calibre Wireless, the device will automatically restart to reclaim memory used by WiFi.

### 3.8 Sleep Screen

![Sleep Screen](images/sleep-screen.jpg)

You can customize the sleep screen by placing custom images in specific locations on the SD card:

- **Single Image:** Place a file named `sleep.bmp` in the root directory.
- **Multiple Images:** Create a `sleep` directory in the root of the SD card and place any number of `.bmp` images
  inside. If images are found in this directory, they will take priority over the `sleep.bmp` file, and one will be
  randomly selected each time the device sleeps.

> [!NOTE]
> You'll need to set the **Sleep Screen** setting to **Custom** in order to use these images.

#### Image Parameters

- **Resolution:** 480 × 800 pixels (portrait mode)
- **Color depth:** 8-bit grayscale or 24-bit color
- **Format:** BMP, uncompressed (BI_RGB)
- **Display levels:** 4 grayscale (black, dark gray, light gray, white)

> [!TIP]
> - Use 8-bit grayscale for best results - it's widely supported by image editors
> - Larger images will be automatically scaled down while preserving aspect ratio
> - All color images are converted to 4-level grayscale on the e-ink display

> [!TIP]
> The **Cover** sleep screen option displays the cover of the currently open book when the device sleeps.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning

- **Previous Page:** Press **Left** or **Volume Up**
- **Next Page:** Press **Right** or **Volume Down**
- **Power Button:** When **Short Power Button** is set to **Page Turn** in Settings, pressing the power button also turns to the next page (convenient for one-handed reading)

### Chapter Navigation
* **Next Chapter:** Press and **hold** the **Right** (or **Volume Down**) button briefly, then release.
* **Previous Chapter:** Press and **hold** the **Left** (or **Volume Up**) button briefly, then release.

### System Navigation
* **Return to Home:** Press **Back** to close the book and return to the Book Selection screen.
* **Chapter Menu:** Press **Confirm** to open the Table of Contents/Chapter Selection screen.

---

## 5. Chapter Selection Screen

![Table of Contents](images/table-of-contents.jpg)

Accessible by pressing **Confirm** while inside a book. The screen header displays the book title.

1.  Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to highlight the desired chapter.
2.  Press **Confirm** to jump to that chapter.
3.  *Alternatively, press **Back** to cancel and return to your current page.*

---

## 6. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following features are **not yet supported** but
are planned for future updates:

* **Tables:** HTML tables are not rendered. A `[Table omitted]` placeholder is shown instead.
* **Image formats:** Only JPEG and PNG images are supported in EPUB. Other formats (GIF, SVG, WebP) show a placeholder.

---

## 7. Customization

For detailed instructions on creating custom themes and adding custom fonts, see the [Customization Guide](customization.md).
