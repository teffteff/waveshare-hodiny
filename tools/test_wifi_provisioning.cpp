// Regression: a development firmware without secrets must reconnect using
// credentials previously provisioned by release firmware, without rewriting NVS.
#include <cassert>
#include "Preferences.h"
#include "WiFi.h"
#include "WifiProvisioning.h"
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
  assert(WiFi.ssid == "test-saved-network");
  assert(WiFi.password == "test-saved-password");
  assert(WiFi.attempts == 1);
  delay(15000);
  wifiProvisioningLoop();
  assert(WiFi.attempts == 2);
  assert(hostshim::store() == before);
  // Open networks have an empty password and must still reconnect.
  preferences.begin("clock-wifi", false);
  preferences.putString("password", "");
  preferences.end();
  wifiProvisioningBegin();
  assert(WiFi.ssid == "test-saved-network");
  assert(WiFi.password.isEmpty());
}
