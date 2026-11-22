#pragma once
#include <Arduino.h>
constexpr int WIFI_STA = 1;
constexpr int WIFI_ALL_CHANNEL_SCAN = 1;
constexpr int WIFI_CONNECT_AP_BY_SIGNAL = 1;
constexpr int WL_CONNECTED = 3;
struct HostWiFi {
  String ssid, password;
  int attempts = 0;     // begin() i reconnect()
  int configWrites = 0; // begin() přepisuje konfiguraci stanice
  int connectedStatus = 0;
  // Jako Arduino: příznak persistent platí jen při inicializaci ovladače.
  bool persistentRequested = true;
  bool driverInitialized = false;
  bool driverFlashStorage = false;
  void mode(int) {
    if (!driverInitialized) {
      driverInitialized = true;
      driverFlashStorage = persistentRequested;
    }
  }
  void setAutoReconnect(bool) {}
  void persistent(bool value) { persistentRequested = value; }
  void setScanMethod(int) {}
  void setSortMethod(int) {}
  void begin(const char *s, const char *p) {
    ssid = s;
    password = p;
    ++attempts;
    ++configWrites;
  }
  bool reconnect() {
    ++attempts;
    return true;
  }
  int status() { return connectedStatus; }
  void disconnect() {}
};
inline HostWiFi WiFi;
