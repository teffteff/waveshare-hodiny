// Regression: a development firmware without secrets must reconnect using
// credentials previously provisioned by release firmware, without rewriting NVS.
#include <cassert>
#include "Preferences.h"
#include "WiFi.h"
#include "WifiProvisioning.h"
#include "FirmwareBuild.h"
#if !FIRMWARE_RELEASE && __has_include("local/secrets.h")
#include "local/secrets.h"
#define TEST_HAS_WIFI_PROFILE 1
#else
#define TEST_HAS_WIFI_PROFILE 0
#endif
void improvSerialServiceSetProvisioned() {}
void improvSerialServiceProvisioningSucceeded() {}
void improvSerialServiceProvisioningFailed() {}
int main() {
  hostPreferencesReset();
  Preferences preferences;
  assert(preferences.begin("clock-wifi", false));
  preferences.putString("ssid", "test-saved-network");
  preferences.putString("password", "test-saved-password");
  preferences.end();
  const auto before = hostshim::store();
  wifiProvisioningBegin();
  assert(wifiProvisioningHasCredentials());
#if TEST_HAS_WIFI_PROFILE
  assert(WiFi.ssid == WIFI_SSID);
  assert(WiFi.password == WIFI_PASSWORD);
#else
  assert(WiFi.ssid == "test-saved-network");
  assert(WiFi.password == "test-saved-password");
#endif
  assert(WiFi.attempts == 1);
  delay(15000);
  wifiProvisioningLoop();
  assert(WiFi.attempts == 2);
#ifdef WIFI_FALLBACK_SSID
  // A profile with a fallback network alternates between both until one works.
  assert(WiFi.ssid == WIFI_FALLBACK_SSID);
  assert(WiFi.password == WIFI_FALLBACK_PASSWORD);
  delay(15000);
  wifiProvisioningLoop();
  assert(WiFi.attempts == 3);
  assert(WiFi.ssid == WIFI_SSID);
  delay(15000);
  wifiProvisioningLoop();
  assert(WiFi.ssid == WIFI_FALLBACK_SSID);
#endif
  assert(hostshim::store() == before);
  // Open networks have an empty password and must still reconnect.
  preferences.begin("clock-wifi", false);
  preferences.putString("password", "");
  preferences.end();
  wifiProvisioningBegin();
#if TEST_HAS_WIFI_PROFILE
  assert(WiFi.ssid == WIFI_SSID);
  assert(WiFi.password == WIFI_PASSWORD);
#else
  assert(WiFi.ssid == "test-saved-network");
  assert(WiFi.password.isEmpty());
#endif
}
