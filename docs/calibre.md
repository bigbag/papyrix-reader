# Calibre Wireless Device Guide

This guide explains how to use the **Calibre Wireless Device** feature to send books directly from Calibre to your Papyrix Reader over WiFi.

## Overview

Calibre Wireless Device allows you to:

- Send books directly from Calibre desktop to your reader
- View the books already on your device from Calibre
- Delete books from your device remotely
- Sync your library without cables or web browsers

This is the fastest and most convenient way to transfer books if you already use Calibre for ebook management.

## Prerequisites

- **Calibre** installed on your computer ([download here](https://calibre-ebook.com/download))
- Your Papyrix Reader device
- Both devices connected to the **same WiFi network**

---

## Step 1: Enable Wireless Device in Papyrix

1. On your Papyrix Reader, go to **Settings**
2. Select **Calibre Wireless**
3. Connect to your WiFi network when prompted
4. Once connected, the screen displays:
   - **IP Address and Port** (e.g., `192.168.1.42:9090`)
   - **Device Name** (e.g., "Papyrix Reader")
   - Status: "Waiting for Calibre..."

Leave the device on this screen while connecting from Calibre.

---

## Step 2: Connect from Calibre Desktop

### Starting the Connection

1. Open **Calibre** on your computer
2. Click the **Connect/Share** button in the toolbar
3. Select **Start wireless device connection**

<img src="./images/calibre/calibre-connect-menu.png" width="400">

### Automatic Discovery

Calibre will automatically scan for wireless devices on your network. Your Papyrix Reader should appear in the device list within a few seconds.

If automatic discovery doesn't work, you can manually enter the IP address:
1. In Calibre, go to **Connect/Share > Start wireless device connection**
2. Click **Manual connect**
3. Enter the IP address shown on your Papyrix (e.g., `192.168.1.42`)
4. Enter the port number (default: `9090`)

### Connection Confirmation

When connected:
- Your Papyrix screen changes to "Connected to Calibre"
- Calibre shows your device in the left sidebar

---

## Step 3: Sending Books

### Single Book

1. Right-click any book in your Calibre library
2. Select **Send to device > Send to main memory**
3. The book transfers wirelessly

### Multiple Books

1. Select multiple books (Ctrl+click or Shift+click)
2. Right-click and select **Send to device > Send to main memory**
3. All selected books transfer in sequence

### Progress Display

While transferring:
- Papyrix shows "Receiving book..." with the title
- A progress bar indicates transfer status
- Transfer speed depends on your WiFi connection

Books are saved to the `/Books/` folder on your SD card.

---

## Step 4: Managing Your Device Library

### Viewing Books on Device

Once connected, Calibre's left sidebar shows:
- **Device** section with your Papyrix Reader
- Click on **Main memory** to see books on your device

### Deleting Books

From Calibre:
1. Click on your device in the sidebar
2. Select the book(s) to delete
3. Right-click and select **Remove books from device**

The book is deleted from your Papyrix's SD card.

---

## Configuration

### Device Settings File

Settings are stored in `/calibre.ini` on your SD card:

```ini
[Settings]
device_name = Papyrix Reader
password =
```

### Available Settings

- **device_name** - How your device appears in Calibre (default: `Papyrix Reader`)
- **password** - Optional password for authentication (default: empty = no password)

### Setting a Password

If you want to require a password for connections:

1. Edit `/calibre.ini` on your SD card:
   ```ini
   [Settings]
   device_name = My E-Reader
   password = mysecretpassword
   ```

2. In Calibre, go to **Preferences > Sharing > Sharing over the net**
3. Set the same password under **Wireless device connection**

Both passwords must match for the connection to work.

---

## Troubleshooting

### Device Not Found in Calibre

**Problem:** Calibre doesn't discover your Papyrix Reader

**Solutions:**
1. Verify both devices are on the **same WiFi network**
2. Check that no firewall is blocking UDP ports 54982, 48123, 39001, 44044, or 59678
3. Try manual connection using the IP address shown on your device
4. Disable VPN if you're using one

### Connection Fails with Password

**Problem:** "Password mismatch" or authentication error

**Solutions:**
1. Ensure the password in `/calibre.ini` matches Calibre's settings exactly
2. Passwords are case-sensitive
3. Try removing the password from both to test the connection

### Transfer Fails Mid-Way

**Problem:** Book transfer stops or times out

**Solutions:**
1. Check WiFi signal strength on your device
2. Move closer to your WiFi router
3. Try with a smaller book first
4. Check SD card has enough free space

### Books Not Showing Up

**Problem:** Transferred books don't appear in file browser

**Solutions:**
1. Books are saved to `/Books/` folder - check there
2. Only EPUB format is supported
3. Try restarting the device after transfer

---

## Technical Details

### Protocol

Papyrix implements the **Calibre Smart Device App** protocol:
- **Discovery:** UDP broadcast on ports 54982, 48123, 39001, 44044, 59678
- **Communication:** TCP connection on port 9090
- **Authentication:** SHA1-based password hashing (optional)

### Supported Operations

- **Send book** - Receive EPUB files from Calibre
- **Get book list** - Report books on device to Calibre
- **Delete book** - Remove books from device
- **Get storage info** - Report free space to Calibre

### Limitations

- Only **EPUB** format is supported
- One connection at a time
- Large libraries may take time to sync

---

## Tips and Best Practices

1. **Keep Calibre updated** - Newer versions have better wireless support
2. **Use good WiFi signal** - Weak signal causes slow or failed transfers
3. **Organize in Calibre first** - Use Calibre's library management, then sync
4. **Set a descriptive device name** - Helps identify your reader in Calibre
5. **Exit when done** - Press Back to disconnect and save battery

---

## Exiting Calibre Wireless Mode

When you're finished:

1. In Calibre, right-click your device and select **Eject this device**
2. On your Papyrix, press the **Back** button
3. The device will automatically restart to reclaim WiFi memory

> **Note:** The automatic restart is required because the ESP32's WiFi stack fragments memory. Without this restart, some features may not work correctly.

---

## Related Documentation

- [User Guide](user_guide.md) - General device operation
- [Web Server Guide](webserver.md) - Alternative file transfer method
- [README](../README.md) - Project overview and features
