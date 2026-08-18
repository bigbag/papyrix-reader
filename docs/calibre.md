# Calibre Wireless Device Guide

This guide shows how to use the **Calibre Wireless Device** feature. You send books from Calibre to your Papyrix Reader through WiFi.

## Overview

Calibre Wireless Device lets you:

- Send books from Calibre desktop to your reader
- See the books that are on your device from Calibre
- Delete books from your device from Calibre
- Sync your library with no cables and no web browsers

This is the fastest method to send books if you already use Calibre for ebook management.

## Prerequisites

- **Calibre** installed on your computer ([download here](https://calibre-ebook.com/download))
- Your Papyrix Reader device
- The two devices connected to the **same WiFi network**

---

## Step 1: Enable Wireless Device in Papyrix

1. From the Home screen, open **Apps** and select **Calibre Wireless**.
2. Connect to your WiFi network when the device tells you.
3. After the connection, the screen shows:
   - **IP Address and Port** (for example, `192.168.1.42:9090`)
   - **Device Name** (for example, "Papyrix Reader")
   - Status: "Waiting for Calibre..."

Keep the device on this screen while you connect from Calibre.

---

## Step 2: Connect from Calibre Desktop

### Starting the Connection

1. Open **Calibre** on your computer.
2. Click the **Connect/Share** button in the toolbar.
3. Select **Start wireless device connection**.

### Automatic Discovery

Calibre scans for wireless devices on your network. Your Papyrix Reader must show in the device list in some seconds.

If automatic discovery does not operate, you can enter the IP address:
1. In Calibre, go to **Connect/Share > Start wireless device connection**.
2. Click **Manual connect**.
3. Enter the IP address shown on your Papyrix (for example, `192.168.1.42`).
4. Enter the port number (default: `9090`).

### Connection Confirmation

When connected:
- Your Papyrix screen changes to "Connected to Calibre".
- Calibre shows your device in the left sidebar.

---

## Step 3: Sending Books

### Single Book

1. Right-click a book in your Calibre library.
2. Select **Send to device > Send to main memory**.
3. The book transfers through WiFi.

### Multiple Books

1. Select more than one book (Ctrl+click or Shift+click).
2. Right-click and select **Send to device > Send to main memory**.
3. All selected books transfer one after the other.

### Progress Display

During transfer:
- Papyrix shows "Receiving book..." with the title.
- A progress bar shows transfer status.
- Transfer speed is from your WiFi connection.

Books are saved to the `/Books/` folder on your SD card.

---

## Step 4: Managing Your Device Library

### Viewing Books on Device

After the connection, the Calibre left sidebar shows:
- **Device** section with your Papyrix Reader
- Click **Main memory** to see books on your device

### Deleting Books

From Calibre:
1. Click your device in the sidebar.
2. Select the book or books to delete.
3. Right-click and select **Remove books from device**.

The book is deleted from the SD card of your Papyrix.

---

## Configuration

### Device Settings File

Settings are in `/config/calibre.ini` on your SD card:

```ini
[Settings]
device_name = Papyrix Reader
password =
```

### Available Settings

- **device_name** - How your device shows in Calibre (default: `Papyrix Reader`)
- **password** - Optional password for authentication (default: empty = no password)

### Setting a Password

If you want a password for connections:

1. Edit `/config/calibre.ini` on your SD card:
   ```ini
   [Settings]
   device_name = My E-Reader
   password = mysecretpassword
   ```

2. In Calibre, go to **Preferences > Sharing > Sharing over the net**.
3. Set the same password in **Wireless device connection**.

The two passwords must match for the connection to operate.

---

## Troubleshooting

### Device Not Found in Calibre

**Problem:** Calibre does not find your Papyrix Reader.

**Solutions:**
1. Make sure the two devices are on the **same WiFi network**.
2. Make sure no firewall blocks UDP ports 54982, 48123, 39001, 44044, or 59678.
3. Try a manual connection with the IP address shown on your device.
4. Set VPN to off if you use one.

### Connection Fails with Password

**Problem:** "Password mismatch" or authentication error.

**Solutions:**
1. Make sure the password in `/config/calibre.ini` matches the Calibre settings exactly.
2. Passwords are case-sensitive.
3. Try removal of the password from the two sides to test the connection.

### Transfer Fails Mid-Way

**Problem:** Book transfer stops or times out.

**Solutions:**
1. Check WiFi signal strength on your device.
2. Move nearer to your WiFi router.
3. Try with a smaller book first.
4. Make sure the SD card has sufficient free space.

### Books Not Showing Up

**Problem:** Transferred books do not show in the file browser.

**Solutions:**
1. Books are saved to the `/Books/` folder. Look there.
2. Only EPUB format is supported.
3. Try a restart of the device after transfer.

---

## Technical Details

### Protocol

Papyrix uses the **Calibre Smart Device App** protocol:
- **Discovery:** UDP broadcast on ports 54982, 48123, 39001, 44044, 59678
- **Communication:** TCP connection on port 9090
- **Authentication:** SHA1-based password hashing (optional)

### Supported Operations

- **Send book** - Receive EPUB files from Calibre
- **Get book list** - Report books on the device to Calibre
- **Delete book** - Remove books from the device
- **Get storage info** - Report free space to Calibre

### Limitations

- Only **EPUB** format is supported.
- One connection at a time.
- Large libraries can take time to sync.

---

## Tips and Best Practices

1. **Keep Calibre updated** - Newer versions have better wireless support.
2. **Use a good WiFi signal** - A weak signal causes slow transfers or failed transfers.
3. **Organize in Calibre first** - Use Calibre library management, then sync.
4. **Set a device name that you can identify** - This helps you find your reader in Calibre.
5. **Exit when you are done** - Press Back to disconnect and save battery.

---

## Exiting Calibre Wireless Mode

When you are done:

1. In Calibre, right-click your device and select **Eject this device**.
2. On your Papyrix, press the **Back** button.
3. The device restarts to get WiFi memory back.

> **Note:** The restart is necessary. The ESP32 WiFi stack fragments memory. With no restart, some features do not operate correctly.

---

## Related Documentation

- [User Guide](user_guide.md) - General device operation
- [Web Server Guide](webserver.md) - Alternative file transfer method
- [README](../README.md) - Project overview and features
