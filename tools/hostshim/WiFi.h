#pragma once
#include <Arduino.h>
constexpr int WIFI_STA = 1;
constexpr int WIFI_ALL_CHANNEL_SCAN = 1;
constexpr int WIFI_CONNECT_AP_BY_SIGNAL = 1;
constexpr int WL_CONNECTED = 3;
struct HostWiFi {
  String ssid, password;
  int attempts = 0;
  void mode(int) {}
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void setScanMethod(int) {}
  void setSortMethod(int) {}
  void begin(const char *s, const char *p) { ssid = s; password = p; ++attempts; }
  int status() { return 0; }
  void disconnect() {}
};
inline HostWiFi WiFi;
