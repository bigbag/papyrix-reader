# Printer

PapyriX works as a driverless network printer. A computer or phone on the same network can print documents, web pages, and photos directly to the e-paper screen.

## Start the printer

Open **Apps → Printer** from the Home screen.

The app connects to WiFi with these rules:

1. If a WiFi network is saved, the app connects to it without asking.
2. If no network is saved, or every saved network fails, the app opens the WiFi picker. The picker lists saved networks, a scan for new networks, and the hotspot option.
3. After you connect, the app starts the print server.

The **hotspot** option starts a WiFi access point named **PapyriX**. Join this network on your computer, then print. The printer screen shows the network name and the printer address.

The waiting screen shows the network name, the IP address, and the printer URI, for example `ipp://192.168.8.154:631/ipp/print`.

Press **Back** to stop the printer and shut the WiFi down.

## Print from a computer

The printer advertises itself through mDNS/Bonjour as **PapyriX**.

- **macOS**: The printer appears in the system print dialog under **PapyriX**. Select it and print. You do not need to install a driver.
- **Windows 10 and 11**: Add the printer in **Settings → Bluetooth & devices → Printers & scanners → Add device**. Windows finds **PapyriX** on the network through the built-in IPP class driver.
- **Linux (CUPS)**: The printer appears in the printing dialog. You can also add it manually:

  ```bash
  lpadmin -p PapyriX -E -v ipp://<device-ip>:631/ipp/print -m everywhere
  ```

- **iPhone and iPad**: The printer appears in the system print share sheet through AirPrint.
- **Android**: The printer works with print services that support IPP Everywhere, for example the Mopria Print Service.

If discovery does not work, use the address shown on the printer screen.

## What can print

Print from any application. The operating system converts the document before it sends it. The page scales to fit the screen with the aspect ratio preserved, centered, and the unused area stays blank.

## Printouts

Each printed page is saved to the `/printouts` directory on the SD card as a 1-bit BMP file.

Browse earlier printouts with the **Left** and **Right** buttons on the printer waiting screen. Printouts also appear in the **Image Viewer** app together with the images in `/images`.

To copy printouts to a computer, use the web server file browser and download from `/printouts`.

## Limits

- One page prints per job. Extra pages in a multi-page print are dropped.
- The maximum job size is 8 MB.
- The printer serves one client at a time.

## Troubleshooting

The printer does not appear in the print dialog:

- Check that the phone or computer is on the same network as the device.
- Networks with client isolation block printer discovery. Use the hotspot instead.
- Add the printer manually with the address from the printer screen.

The printer stops when the device sleeps:

- Open the printer app again after wake. The app blocks auto-sleep while it runs, so sleep starts only from the power button.

See the [web server guide](webserver.md) for the file browser and the [Calibre guide](calibre.md) for book transfer.
