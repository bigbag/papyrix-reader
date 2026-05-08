#include "NetworkViews.h"

#include <I18n.h>
#include <qrcode.h>

#include <cstdio>

namespace ui {

namespace {

constexpr int QR_VERSION = 4;
constexpr int QR_MODULES = 4 * QR_VERSION + 17;  // QR version formula
constexpr int QR_MODULE_SIZE = 6;
constexpr int QR_PADDING = 6;

void drawQRCode(const GfxRenderer& r, int x, int y, const char* data, bool fgBlack) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
  qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, data);

  constexpr int moduleSize = QR_MODULE_SIZE;
  const int qrSize = qrcode.size * moduleSize;

  // Draw background with padding
  r.fillRect(x - QR_PADDING, y - QR_PADDING, qrSize + QR_PADDING * 2, qrSize + QR_PADDING * 2, !fgBlack);

  // Draw QR modules
  for (uint8_t row = 0; row < qrcode.size; row++) {
    for (uint8_t col = 0; col < qrcode.size; col++) {
      if (qrcode_getModule(&qrcode, col, row)) {
        r.fillRect(x + col * moduleSize, y + row * moduleSize, moduleSize, moduleSize, fgBlack);
      }
    }
  }
}

}  // namespace

void render(const GfxRenderer& r, const Theme& t, const NetworkModeView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(NETWORK_MODE));

  const char* items[] = {tr(JOIN_NETWORK), tr(CREATE_HOTSPOT)};
  const int startY = 100;
  for (int i = 0; i < NetworkModeView::ITEM_COUNT; i++) {
    const int y = startY + i * (t.itemHeight + 20);
    menuItem(r, t, y, items[i], i == v.selected);
  }

  const int descY = startY + 2 * (t.itemHeight + 20) + 40;
  if (v.selected == 0) {
    centeredText(r, t, descY, tr(CONNECT_WIFI));
  } else {
    centeredText(r, t, descY, tr(CREATE_WIFI_HOTSPOT));
  }

  ButtonBar btns{tr(BACK), tr(SELECT), "", ""};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const WifiListView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(SELECT_NETWORK));

  if (v.scanning) {
    const int centerY = r.getScreenHeight() / 2;
    centeredText(r, t, centerY, v.statusText);
  } else if (v.networkCount == 0) {
    const int centerY = r.getScreenHeight() / 2;
    centeredText(r, t, centerY, tr(NO_NETWORKS_FOUND));
    centeredText(r, t, centerY + 30, tr(PRESS_CONFIRM_SCAN));
  } else {
    const int listStartY = 60;
    const int pageStart = v.getPageStart();
    const int pageEnd = v.getPageEnd();

    for (int i = pageStart; i < pageEnd; i++) {
      const int y = listStartY + (i - pageStart) * (t.itemHeight + t.itemSpacing);
      wifiEntry(r, t, y, v.networks[i].ssid, v.networks[i].signal, v.networks[i].secured, i == v.selected);
    }
  }

  ButtonBar btns{tr(BACK), tr(CONNECT), "", tr(SCAN)};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const WifiConnectingView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(CONNECTING_TITLE));

  const int centerY = r.getScreenHeight() / 2 - 60;

  centeredText(r, t, centerY, v.ssid);
  centeredText(r, t, centerY + 40, v.statusMsg);

  if (v.status == WifiConnectingView::Status::Connected) {
    char ipLine[32];
    snprintf(ipLine, sizeof(ipLine), tr(FMT_IP), v.ipAddress);
    centeredText(r, t, centerY + 80, ipLine);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const WebServerView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(WEB_SERVER));

  if (v.serverRunning) {
    // Build URL string
    char urlStr[32];
    snprintf(urlStr, sizeof(urlStr), "http://%s/", v.ipAddress);

    const int screenWidth = r.getScreenWidth();
    constexpr int qrSize = QR_MODULES * QR_MODULE_SIZE + QR_PADDING * 2;

    if (v.isApMode) {
      // AP mode: WiFi QR centered, AP name and URL as text below
      char wifiQR[64];
      snprintf(wifiQR, sizeof(wifiQR), "WIFI:S:%s;;", v.ssid);

      const int qrX = (screenWidth - qrSize) / 2;
      const int qrY = 80;

      drawQRCode(r, qrX + QR_PADDING, qrY + QR_PADDING, wifiQR, t.primaryTextBlack);

      const int labelY = qrY + qrSize + 15;
      centeredText(r, t, labelY, v.ssid);
      centeredText(r, t, labelY + 30, urlStr);
    } else {
      // STA mode: show single URL QR code centered
      const int qrX = (screenWidth - qrSize) / 2;
      const int qrY = 80;

      drawQRCode(r, qrX + QR_PADDING, qrY + QR_PADDING, urlStr, t.primaryTextBlack);

      // Label and network info below
      const int labelY = qrY + qrSize + 15;
      centeredText(r, t, labelY, urlStr);
      centeredText(r, t, labelY + 30, v.ssid);
    }
  } else {
    centeredText(r, t, 180, tr(SERVER_STOPPED));
  }

  ButtonBar btns{tr(STOP)};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

}  // namespace ui
