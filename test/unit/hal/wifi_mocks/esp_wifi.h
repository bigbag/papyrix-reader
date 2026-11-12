#pragma once

#include <WiFi.h>

constexpr int WIFI_IF_AP = 1;
constexpr int WIFI_BW_HT20 = 1;
constexpr int WIFI_PS_NONE = 0;
inline int esp_wifi_set_ps(int) { return 0; }
inline int esp_wifi_set_bandwidth(int, int) { return WiFi.failBandwidth ? -1 : 0; }
