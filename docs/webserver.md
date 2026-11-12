# Web Server Guide

The built-in web server provides file upload and management through Wi-Fi.

## Overview

PapyriX Reader includes a built-in web server that lets you:

- Upload books through WiFi from a device on the same WiFi network
- Browse and manage files on the SD card of your device
- Create folders to organize your ebooks
- Delete files and folders
- Upload and manage custom sleep screen images
- Upload custom fonts and font themes

## Prerequisites

- Your PapyriX Reader device
- A WiFi network
- A computer, phone, or tablet connected to the **same WiFi network**

---

## Step 1: Accessing File Transfer

1. Open **Apps > File Transfer** or **Settings > Device > WiFi**.
2. Select **Join Network** to scan, or select **Recent** to try saved networks in order.

---

## Step 2: Connecting to WiFi

### Viewing Available Networks

After the scan completes, you see a list of available WiFi networks with these indicators:

- **Signal strength bars** (`||||`, `|||`, `||`, `|`) - Shows connection quality
- **`*` symbol** - Shows that the network has a password (encrypted)
- **`+` symbol** - Shows that you saved credentials for this network before

### Selecting a Network

1. Use the **Left/Right** (or **Volume Up/Down**) buttons to go through the network list.
2. Press **Confirm** to select the highlighted network.

### Entering Password (for encrypted networks)

If the network needs a password:

1. An on-screen keyboard shows with a full character grid.
2. The keyboard is organized in zones:
   - **Rows 1-3:** Lowercase letters (a-z) and usual symbols (. - _ @)
   - **Rows 4-6:** Uppercase letters (A-Z) and symbols (! # $ %)
   - **Row 7:** Numbers (0-9)
   - **Row 8:** More symbols (^ & * ( ) + [ ] \)
   - **Bottom row:** SPACE and BACKSPACE controls
3. Use **Up/Down/Left/Right** to move on the grid.
4. Press **Confirm** to enter the selected character.
5. Press **Back** to cancel and go back.

**Note:** If you connected to this network before, the saved password is used.

### Connection Process

The device shows "Connecting..." while it makes the connection. Each network has a 15-second limit. Press **Back** (**Cancel**) to stop the attempt.

If you cancel **Recent**, the device returns to network mode selection. If you cancel a connection from **Saved networks**, the device returns to that network's actions. A failed or cancelled attempt stops the radio before another connection starts.

### Saving Credentials

In **Apps > File Transfer**, the device asks you to save the password for a new network:

- Select **Yes** to save credentials for the next connection. The device stores passwords on the SD card with reversible obfuscation, not encryption. Do not save sensitive passwords.
- Select **No** to connect with no save

In **Settings > Device > WiFi**, the device saves a new password after a successful connection. Use **Saved networks > Add network** to save a password without connecting.

The credential file format does not change. Valid credentials from older firmware remain usable. The device rejects incomplete records and lengths above the supported limits. A failed load does not overwrite the file or replace the last complete credentials in memory.

---

## Step 3: Connection Success

After the connection, the screen shows:

- **Network name** (SSID)
- **IP Address** (for example, `192.168.1.102`)
- **Web server URL** (for example, `http://192.168.1.102/`)

**Important:** Record the IP address. You need this to open the web interface from your computer or phone.

---

## Step 4: Accessing the Web Interface

### From a Computer

1. Connect the computer to the **same WiFi network** as the reader.
2. Open a web browser.
3. Type the IP address shown on your device into the address bar of the browser.
   - Example: `http://192.168.1.102/`
4. Press Enter.

### From a Phone or Tablet

1. Connect the phone or tablet to the **same WiFi network** as the reader.
2. Open your mobile browser (Safari, Chrome, or other).
3. Type the IP address into the address bar.
   - Example: `http://192.168.1.102/`
4. Tap Go.

---

## Step 5: Using the Web Interface

The web interface uses a tab layout with six tabs: **Books**, **Sleep**, **Fonts**, **Themes**, **Locale**, and **Firmware**. The firmware version is shown in the top-right corner.

![Books tab](images/web-books.png)

### Books Tab

The Books tab is the default view. It shows files and folders on the SD card root.

- **Folders** are shown with an orange **DIR** badge and a trash icon
- **Files** show their size (for example, "1.2 MB") and a trash icon
- Click a folder name to go into it
- The path breadcrumb (for example, "SD" or "/books") shows the current location

#### Uploading Books

1. Click the **Upload** button.
2. In the dialog, click **Choose File** and select a file.
3. Click **Upload**.
4. The page refreshes when the upload completes.

![Upload dialog (Books)](images/web-upload-books.png)

**Supported book formats:** `.epub`, `.fb2`, `.html`, `.htm`, `.xtc`, `.xtch`, `.xtg`, `.xth`, `.txt`, `.text`, `.md`, `.markdown`

**Supported image formats:** `.jpg`, `.jpeg`, `.png`, `.bmp`

**Note:** Files with uppercase extensions (for example, `Book.EPUB`) are changed to lowercase on upload. Unsupported file types are rejected.

The server writes each received multipart block to a temporary file in `/.papyrix`. It does not allocate a second upload buffer. A failed write, failed sync, or cancelled transfer removes only that temporary file. The existing destination stays unchanged until the complete upload is ready. This rule also applies to locale and firmware uploads.

The server keeps a backup while it replaces an existing destination. If replacement fails, it tries to restore the backup. A rename can report failure after it moves a file, so the server checks the file paths during recovery. If the original stays at the backup path, the error response gives that path. Recover that file with an SD card reader.

A power loss during replacement can leave `.upload-*.tmp` or `.upload-*.bak` files in `/.papyrix`. Replacement is not power-loss atomic.

#### Creating Folders

1. Click the **New Folder** button.
2. Enter a supported folder name.
3. Click **Create**.

![New Folder dialog](images/web-new-folder.png)

This is useful to organize your ebooks by genre, author, or series.

#### Deleting Files and Folders

1. Click the trash icon adjacent to a file or folder.
2. Confirm the deletion in the popup dialog.

**Warning:** Web File Manager deletion is permanent. You cannot reverse it. The **Recycle bin** setting of the device only has an effect on deletion from the Files screen on the device.

**Note:** Web File Manager folders must be empty before you can delete them. Use the Files screen on the device to delete a folder that is not empty.

### Unicode filenames

Folder create, upload, and rename support UTF-8 names in Latin (including Vietnamese), Cyrillic, Greek, Thai, and Arabic. Names are changed to NFC before storage.

Web mutations reject:

- control characters and `" * / : < > ? \ |`;
- names that start with a dot, `.` and `..`;
- names longer than 255 UTF-8 bytes;
- complete paths longer than 1023 bytes;
- CJK filenames, because the device file-browser font cannot show them.

Invalid names return HTTP `400`. Upload validation occurs before the destination file is opened. Rejected uploads leave no partial file.

### Sleep Tab

The Sleep tab manages custom sleep screen images in the `/sleep` directory.

![Sleep tab](images/web-sleep.png)

- Each file shows its name and size
- A description at the top tells you to set sleep mode to "Custom" in device settings

#### Uploading Sleep Screens

1. Click the **Upload** button.
2. Select a `.bmp` file from your device.
3. Click **Upload**.

![Upload dialog (Sleep)](images/web-upload.png)

**Note:** Only `.bmp` files are accepted. Use the [sleep screen converter](../README.md) (`make sleep-screen`) to convert images to the correct format.

**Note:** Set **Sleep Screen** to **Custom** on the device to use uploaded images. **Random** selects an image each time the device sleeps. To use one image, open **Settings → Screen → Sleep Image** on the device and select its filename. See the [User Guide](user_guide.md#37-sleep-screen) for selection and fallback behavior.

### Fonts Tab

The Fonts tab manages custom font directories in `/fonts` on the SD card.

![Fonts tab](images/web-fonts.png)

- Font directories are shown with **DIR** badges (for example, `literata-14`, `noto-sans-16`)
- Use **Upload** to add `.epdfont` font files
- Use **New Folder** to create new font directories

See the [Fonts Guide](fonts.md) and [Customization Guide](customization.md) for how to make and use custom fonts.

### Themes Tab

The Themes tab manages `.theme` files for custom CJK font themes.

![Themes tab](images/web-themes.png)

- Each `.theme` file shows its name and size
- Use **Upload** to add new theme files

See the [Customization Guide](customization.md) for how to make and use custom themes.

### Locale Tab

The Locale tab manages custom locale (language) files for the device UI.

- Shows if a custom locale file is installed
- Use **Upload** to add a custom locale file
- Use **Delete** to remove the installed locale and go back to built-in translations

See `docs/examples/locale/` in the repository for locale file examples.

### Firmware Tab

The Firmware tab manages firmware update files for the device.

- Shows if a firmware file is on the SD card (`firmware.bin`)
- Use **Upload** to add a `.bin` firmware file
- Use **Delete** to remove the firmware file
- A note tells you to run the update from **Settings > Firmware Update** on the device

---

## Troubleshooting

### Cannot See the Device on the Network

**Problem:** Browser shows "Cannot connect" or "Site can't be reached"

**Solutions:**

1. Connect both devices to the **same WiFi network**.
   - Check your computer/phone WiFi settings.
   - Check that the reader shows "Connected".
2. Check the IP address again.
   - Check the address for typing errors.
   - Include `http://` at the start.
3. Try to set VPN to off if you use one.
4. Some networks have "client isolation" on. Check with your network administrator.

### Connection Drops or Times Out

**Problem:** WiFi connection is not stable.

**Solutions:**

1. Move nearer to the WiFi router.
2. Check signal strength on the device (must be `||` or better).
3. Prevent interference from other devices.
4. Try a different WiFi network if one is available.

### Upload Fails

**Problem:** File upload does not complete or shows an error.

**Solutions:**

1. Check that the file uses a supported format.
2. Check the free space on the SD card.
3. Try to upload a smaller file first to test.
4. Refresh the browser page and try again.

### Saved Password Not Working

**Problem:** Device does not connect with saved credentials.

**Solutions:**

1. Open **Settings > Device > WiFi > Saved networks**.
2. Select the network and edit its password, or forget the network.
3. Connect again with the correct password.

### WiFi Connection Errors

Serial logs report startup failures, rejected connection attempts, and connection timeouts. They do not include passwords.

Internal Disk usage measures storage, not the RAM that WiFi needs.

---

## Security Notes

- The web server runs on port 80 (standard HTTP).
- The server has no authentication. Anyone on the same network can access it.
- The web server is only available while the WiFi screen shows "Connected".
- The web server stops when you exit the WiFi screen.
- For security, use only trusted private networks.

---

## Technical Details

- **Supported WiFi:** 2.4GHz networks (802.11 b/g/n)
- **Web Server Port:** 80 (HTTP)
- **Connection queue:** The listener uses the SDK accept-queue capacity. The server handles one request at a time.
- **Maximum Upload Size:** Limited by available SD card space
- **Supported File Formats:** `.epub`, `.fb2`, `.html`, `.htm`, `.xtc`, `.xtch`, `.xtg`, `.xth`, `.txt`, `.text`, `.md`, `.markdown` (books); `.jpg`, `.jpeg`, `.png`, `.bmp` (images); `.epdfont` (fonts); `.theme` (themes)
- **Browser Compatibility:** All modern browsers (Chrome, Firefox, Safari, Edge)
- **Memory use:** Network entry releases bitmap row buffers and the theme cache. Directory listings encode each entry in a fixed buffer without JSON heap allocations. Buffered TCP writes reduce memory use during page loads. Server shutdown releases query and form arguments.
- **Allocation limits:** The server checks allocation of its two owned server objects. SDK constructors and route registration can still fail if RAM is exhausted. SDK receive buffers can also fail under load.
- **HTTP request limits:** Protocol lines have a 4096-byte limit. Oversized request lines return HTTP 414. Oversized HTTP header lines return HTTP 431. Each line has a 5-second deadline. The request line and all HTTP headers share a 10-second deadline. An incomplete request returns HTTP 408. These deadlines do not limit the total file upload time.
- **Argument limits:** The parser accepts the first 256 query arguments. It ignores additional arguments. Multipart forms accept up to 32 arguments. Each non-file field has a 16384-byte limit. Multipart headers and non-file fields share a 10-second deadline. The deadline restarts after each file payload. Invalid multipart data closes the connection.

The platform pin in `platformio.ini` selects the Arduino-ESP32 version. Build with PlatformIO Core 6.2.0 or later. Run `uv sync` to install the project requirements.

`scripts/patch_webserver.py` applies a [WebServer parser backport](https://github.com/espressif/arduino-esp32/commit/5cdf8975ae8d9e35888b724b01a444d22406424e) to Arduino-ESP32 3.3.7. It compiles a generated source file in each environment's build directory. A SCons object emitter selects this source after compiler setup. It does not change the installed SDK or the WiFi stack. The build rejects an unknown parser source. Review the backport before you change the SDK version.

Run the read-only request regression while WiFi Transfer is active:

```bash
python3 test/scripts/WebServerSmoke.py --host <reader-ip>
```

The multipart checks require connection closure without an HTTP response.

On Linux, add concurrent page, status, and directory requests with an ICMP check:

```bash
python3 test/scripts/WebServerSmoke.py --host <reader-ip> --load-rounds 120
```

Run this check with files on the SD card. It requires `ping`. It fails on HTTP errors, invalid responses, a restart, or five consecutive missing ping replies. It does not write files to the reader.

On a load failure, the check reports the round, elapsed time, and ping output. Invalid JSON responses include the response body. These logs can contain filenames.

To check a file upload that takes more than 10 seconds:

```bash
python3 test/scripts/WebServerSmoke.py --host <reader-ip> --upload-check
```

This option writes a uniquely named 64 KB test file. It checks the downloaded bytes and file metadata. It attempts deletion even if directory listing fails. The check keeps an earlier failure and reports any cleanup error as a warning. A cleanup error fails an otherwise successful check. The default check does not create files.

Run `pio pkg install -e default` before the host parser and build regressions. The build regression compiles a consumer through normal and pinned Windows dispatchers. It fails if parser substitution does not enforce the fixture's limit. Run `make test-tools` to execute the regressions.

---

## Exiting WiFi Mode

When you are done with file upload:

1. Press the **Back** button on your PapyriX Reader.
2. The web server stops.
3. WiFi disconnects.
4. **The device restarts** to get memory back.

---

## Related Documentation

- [User Guide](user_guide.md) - General device operation
- [Customization Guide](customization.md) - Custom themes and fonts
- [README](../README.md) - Project overview and features
