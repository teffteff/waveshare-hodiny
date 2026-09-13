#include "WifiProvisioning.h"

#include <Preferences.h>
#include <WiFi.h>

#include "FirmwareBuild.h"
#include "ImprovSerialService.h"

#if !FIRMWARE_RELEASE && __has_include("local/secrets.h")
#include "local/secrets.h"
#define HAS_DEVELOPMENT_WIFI 1
#else
#define HAS_DEVELOPMENT_WIFI 0
#endif

namespace {
constexpr char WIFI_NAMESPACE[] = "clock-wifi";
constexpr char WIFI_SSID_KEY[] = "ssid";
constexpr char WIFI_PASSWORD_KEY[] = "password";
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t PROVISIONING_TIMEOUT_MS = 30000;

String storedSsid;
String storedPassword;
// Optional second network from a development profile, tried in turn with the
// stored one until either connects. Always empty in release builds.
String fallbackSsid;
String fallbackPassword;
bool useFallback = false;
String pendingSsid;
String pendingPassword;
unsigned long lastWifiAttempt = 0;
unsigned long provisioningStartedAt = 0;
bool provisioningActive = false;

void loadStoredCredentials() {
#if HAS_DEVELOPMENT_WIFI
  // An explicitly selected home/work build profile takes precedence.
  storedSsid = WIFI_SSID;
  storedPassword = WIFI_PASSWORD;
#ifdef WIFI_FALLBACK_SSID
  fallbackSsid = WIFI_FALLBACK_SSID;
  fallbackPassword = WIFI_FALLBACK_PASSWORD;
#endif
#else
  // Builds without local credentials can reuse Wi-Fi provisioned by a release.
  storedSsid = "";
  storedPassword = "";
  Preferences preferences;
  if (preferences.begin(WIFI_NAMESPACE, true)) {
    storedSsid = preferences.getString(WIFI_SSID_KEY);
    storedPassword = preferences.getString(WIFI_PASSWORD_KEY);
    preferences.end();
  }
#endif
}

bool saveStoredCredentials(const String &ssid, const String &password) {
#if FIRMWARE_RELEASE
  Preferences preferences;
  if (!preferences.begin(WIFI_NAMESPACE, false)) return false;
  const bool ok = preferences.putString(WIFI_SSID_KEY, ssid) == ssid.length() &&
                  preferences.putString(WIFI_PASSWORD_KEY, password) ==
                      password.length();
  preferences.end();
  return ok;
#else
  return true;
#endif
}

void beginWifiConnection(const String &ssid, const String &password) {
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid.c_str(), password.c_str());
}

void connectStoredCredentials() {
  if (storedSsid.isEmpty()) return;
  if (useFallback) {
    beginWifiConnection(fallbackSsid, fallbackPassword);
  } else {
    beginWifiConnection(storedSsid, storedPassword);
  }
  lastWifiAttempt = millis();
}
}  // namespace

void wifiProvisioningBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  loadStoredCredentials();
  useFallback = false;
  connectStoredCredentials();
}

void wifiProvisioningLoop() {
  if (provisioningActive) {
    if (WiFi.status() == WL_CONNECTED) {
      if (saveStoredCredentials(pendingSsid, pendingPassword)) {
        storedSsid = pendingSsid;
        storedPassword = pendingPassword;
        provisioningActive = false;
        pendingSsid = "";
        pendingPassword = "";
        improvSerialServiceProvisioningSucceeded();
        return;
      }
      WiFi.disconnect();
      provisioningActive = false;
      pendingSsid = "";
      pendingPassword = "";
      improvSerialServiceProvisioningFailed();
      connectStoredCredentials();
      return;
    }
    if (millis() - provisioningStartedAt >= PROVISIONING_TIMEOUT_MS) {
      WiFi.disconnect();
      provisioningActive = false;
      pendingSsid = "";
      pendingPassword = "";
      improvSerialServiceProvisioningFailed();
      connectStoredCredentials();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // After a drop, auto-reconnect gets a full retry window on the network
    // that worked before the loop starts alternating again.
    if (!fallbackSsid.isEmpty()) lastWifiAttempt = millis();
    improvSerialServiceSetProvisioned();
    return;
  }
  if (!storedSsid.isEmpty() && millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
    if (!fallbackSsid.isEmpty()) useFallback = !useFallback;
    connectStoredCredentials();
  }
}

void wifiProvisioningStart(const String &ssid, const String &password) {
#if FIRMWARE_RELEASE
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64) {
    improvSerialServiceProvisioningFailed();
    return;
  }
  pendingSsid = ssid;
  pendingPassword = password;
  provisioningActive = true;
  provisioningStartedAt = millis();
  WiFi.disconnect();
  beginWifiConnection(pendingSsid, pendingPassword);
  lastWifiAttempt = millis();
#else
  (void)ssid;
  (void)password;
#endif
}

bool wifiProvisioningHasCredentials() { return !storedSsid.isEmpty(); }

bool wifiProvisioningIsActive() { return provisioningActive; }
