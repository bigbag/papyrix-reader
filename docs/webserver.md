# Web Server Guide

This guide shows how to connect your Papyrix Reader to WiFi and how to use the built-in web server to upload books from your computer or phone.

## Overview

Papyrix Reader includes a built-in web server that lets you:

- Upload books through WiFi from a device on the same WiFi network
- Browse and manage files on the SD card of your device
- Create folders to organize your ebooks
- Delete files and folders
- Upload and manage custom sleep screen images
- Upload custom fonts and font themes

## Prerequisites

- Your Papyrix Reader device
- A WiFi network
- A computer, phone, or tablet connected to the **same WiFi network**

---

## Step 1: Accessing File Transfer

1. From the Home screen, open **Apps** and select **File Transfer**.
2. The device starts a scan for available networks.

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

The device shows "Connecting..." while it makes the connection. This usually takes 5-10 seconds.

### Saving Credentials

If this is a new network, the device asks you to save the password:

- Select **Yes** to save credentials for automatic connection the next time (NOTE: These are stored as plaintext on the SD card of the device. Do not use this for networks that are sensitive.)
- Select **No** to connect with no save

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

1. Make sure your computer is connected to the **same WiFi network** as your Papyrix Reader.
2. Open a web browser (Chrome is recommended).
3. Type the IP address shown on your device into the address bar of the browser.
   - Example: `http://192.168.1.102/`
4. Press Enter.

### From a Phone or Tablet

1. Make sure your phone/tablet is connected to the **same WiFi network** as your Papyrix Reader.
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

#### Creating Folders

1. Click the **New Folder** button.
2. Enter a supported folder name.
3. Click **Create**.

![New Folder dialog](images/web-new-folder.png)

This is useful to organize your ebooks by genre, author, or series.

#### Deleting Files and Folders

1. Click the trash icon adjacent to a file or folder.
2. Confirm the deletion in the popup dialog.

**Warning:** Web File Manager deletion is permanent. You cannot reverse it. The **Recycle bin** setting of the device only
has an effect on deletion from the Files screen on the device.

**Note:** Web File Manager folders must be empty before you can delete them. Use the Files screen on the device to delete
a folder that is not empty.

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

**Note:** Set the **Sleep Screen** setting to **Custom** on the device to use uploaded images. A random image is shown each time the device sleeps. See the [User Guide](user_guide.md#37-sleep-screen) for more data.

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

1. Make sure the two devices are on the **same WiFi network**.
   - Check your computer/phone WiFi settings.
   - Make sure the Papyrix Reader shows "Connected" status.
2. Check the IP address again.
   - Make sure you typed it correctly.
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

1. Make sure the file is a supported format (`.epub`, `.fb2`, `.html`, `.txt`, `.md`, and more).
2. Make sure the SD card has sufficient free space.
3. Try to upload a smaller file first to test.
4. Refresh the browser page and try again.

### Saved Password Not Working

**Problem:** Device does not connect with saved credentials.

**Solutions:**

1. When connection fails, the device asks you to "Forget Network".
2. Select **Yes** to remove the saved password.
3. Connect again and enter the password again.
4. Select to save the new password.

---

## Security Notes

- The web server runs on port 80 (standard HTTP).
- **No authentication is necessary** - a person on the same network can open the interface.
- The web server is only available while the WiFi screen shows "Connected".
- The web server stops when you exit the WiFi screen.
- For security, use only trusted private networks.

---

## Technical Details

- **Supported WiFi:** 2.4GHz networks (802.11 b/g/n)
- **Web Server Port:** 80 (HTTP)
- **Maximum Upload Size:** Limited by available SD card space
- **Supported File Formats:** `.epub`, `.fb2`, `.html`, `.htm`, `.xtc`, `.xtch`, `.xtg`, `.xth`, `.txt`, `.text`, `.md`, `.markdown` (books); `.jpg`, `.jpeg`, `.png`, `.bmp` (images); `.epdfont` (fonts); `.theme` (themes)
- **Browser Compatibility:** All modern browsers (Chrome, Firefox, Safari, Edge)

---

## Tips and Best Practices

1. **Organize with folders** - Create folders before you upload to keep your library organized.
2. **Check signal strength** - Stronger signals (`|||` or `||||`) give faster, more reliable uploads.
3. **Upload more than one file** - You can upload files one at a time. The page refreshes after each upload.
4. **Use names that you can identify** - Name your folders clearly (for example, "SciFi", "Mystery", "Non-Fiction").
5. **Keep credentials saved** - Save your WiFi password for a fast connection later.
6. **Exit when you are done** - Press **Back** to exit the WiFi screen and save battery.

---

## Exiting WiFi Mode

When you are done with file upload:

1. Press the **Back** button on your Papyrix Reader.
2. The web server stops.
3. WiFi disconnects.
4. **The device restarts** to get memory back.

> **Note:** The restart is necessary. The ESP32 WiFi stack fragments memory in a way that you cannot recover. With no restart, XTC books can fail to load with a "Memory error". The restart is fast. Your uploaded files are immediately available in the file browser.

---

## Related Documentation

- [User Guide](user_guide.md) - General device operation
- [Customization Guide](customization.md) - Custom themes and fonts
- [README](../README.md) - Project overview and features
