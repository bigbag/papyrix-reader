# LocalSend

PapyriX receives files and books from the LocalSend app.
LocalSend is an open source app for Android, iOS, macOS, Windows, and Linux.
It sends files to nearby devices over WiFi without a central server.

## Start the receiver

Open **Apps → LocalSend** from the Home screen.

The app connects to WiFi with these rules:

1. If a WiFi network is saved, the app connects to it without asking.
2. If no network is saved, or every saved network fails, the app opens the
   WiFi picker. The picker lists saved networks, a scan for new networks, and
   the hotspot option.
3. After you connect, the app starts the receive server.

The **hotspot** option starts a WiFi access point named **PapyriX**.
Join this network on your computer or phone, then send files.

The waiting screen shows the device name: **PapyriX**.

Press **Back** to stop the receiver and shut the WiFi down.

## Send files

1. Turn **Encryption** off in the LocalSend settings on the sender. The
   device uses plain HTTP.
2. Open the LocalSend app on your computer or phone.
3. Select the files you want to send.
4. Select the PapyriX device from the device list.
5. Confirm on the sender. The device accepts every incoming request.

Received files go to `/received` on the SD card. Books open from the
Home screen like every other book.

The receive screen shows the number of received files and the name of the
last file.

## Notes

* The receiver does not ask for approval. It does not need a password. Any
  device that can reach the receiver can send files. Use a trusted network.
  Stop the receiver after the transfer. The device hotspot is open. It has
  no password.
* One sender session at a time. A new prepare request during an open
  session is rejected with 409 until the session completes, is cancelled,
  or expires.
* A session ends when every accepted file arrives. It also expires after
  60 seconds without activity. Each file transfer resets the timer.
* Upload connections have no total time cap once the file body starts. A
  transfer that stalls for 2.5 seconds is aborted. Other requests allow
  120 seconds.
* File uploads accept a `Content-Length` body or a chunked body. Other routes
  do not accept chunked bodies. The receiver removes an incomplete file when
  framing or storage fails. It does not count that file as received.
* File names are made safe for the SD card: the device takes the base name,
  removes unsafe characters, and truncates to 128 characters.
* The device accepts a maximum of 16 files per session.
* A preparation request can contain up to 16 KiB of JSON. The receiver reads
  it in small blocks instead of keeping the complete request in RAM.

## Logo

LocalSend and its mark belong to the LocalSend project. This mark identifies
protocol compatibility. PapyriX is not affiliated with or endorsed by LocalSend.

Project: https://localsend.org
Source: `images/localsend-logo.webp`. Converted asset: `src/images/LocalsendLogo.h`.
