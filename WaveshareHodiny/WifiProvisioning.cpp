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
constexpr char WIFI_PENDING_SSID_KEY[] = "next-ssid";
constexpr char WIFI_PENDING_PASSWORD_KEY[] = "next-password";
constexpr char WIFI_PENDING_VALID_KEY[] = "next-valid";
// Před prvním připojením se zkouší každých 15 s (střídání se záložní sítí
// musí stihnout okno při startu). Po výpadku za provozu se o spojení stará
// hlavně automatické znovupřipojení ovladače, takže vlastní pokus jen jednou
// za minutu, aby se s ním nepraly.
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t WIFI_DROP_RETRY_MS = 60000;
constexpr uint32_t PROVISIONING_TIMEOUT_MS = 30000;

String storedSsid;
String storedPassword;
// Network tried at startup: the stored one, or credentials entered in the
// onboarding portal that have not connected yet.
String startupSsid;
String startupPassword;
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
bool startupCandidateActive = false;
// Whether the current connection attempt uses the portal candidate rather
// than the stored network.
bool connectingCandidate = false;
bool normalStartReady = false;
bool startupRetriesEnabled = true;
// The portal's first background attempt goes to the stored network, because
// the candidate has just failed for the whole startup window.
bool portalTryStored = true;
// Po prvním úspěšném připojení platí delší interval WIFI_DROP_RETRY_MS.
bool connectedOnce = false;
// Otisk sítě, kterou má ovladač právě nastavenou (0 = žádná). Další pokus na
// stejnou síť jde přes WiFi.reconnect(), protože WiFi.begin() pokaždé
// přepisuje konfiguraci stanice (esp_wifi_set_config) a zastaví při tom cache
// flash. Otisk místo kopie jména a hesla šetří interní RAM.
uint32_t configuredNetwork = 0;

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
#if FIRMWARE_RELEASE
    if (preferences.getBool(WIFI_PENDING_VALID_KEY, false)) {
      startupSsid = preferences.getString(WIFI_PENDING_SSID_KEY);
      startupPassword = preferences.getString(WIFI_PENDING_PASSWORD_KEY);
      startupCandidateActive = !startupSsid.isEmpty();
    }
#endif
    preferences.end();
  }
#endif

  if (!startupCandidateActive) {
    startupSsid = storedSsid;
    startupPassword = storedPassword;
  }
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

bool clearPendingCredentials() {
#if FIRMWARE_RELEASE
  Preferences preferences;
  if (!preferences.begin(WIFI_NAMESPACE, false)) return false;
  const bool ok = preferences.remove(WIFI_PENDING_VALID_KEY);
  preferences.remove(WIFI_PENDING_SSID_KEY);
  preferences.remove(WIFI_PENDING_PASSWORD_KEY);
  preferences.end();
  return ok;
#else
  return true;
#endif
}

bool savePendingCredentials(const String &ssid, const String &password) {
#if FIRMWARE_RELEASE
  Preferences preferences;
  if (!preferences.begin(WIFI_NAMESPACE, false)) return false;
  preferences.remove(WIFI_PENDING_VALID_KEY);
  const bool valuesWritten =
      preferences.putString(WIFI_PENDING_SSID_KEY, ssid) == ssid.length() &&
      preferences.putString(WIFI_PENDING_PASSWORD_KEY, password) ==
          password.length();
  const bool ok =
      valuesWritten && preferences.putBool(WIFI_PENDING_VALID_KEY, true) == 1;
  preferences.end();
  return ok;
#else
  (void)ssid;
  (void)password;
  return false;
#endif
}

// Development builds may pin the main network to a fixed address from .env,
// because a stray DHCP server on the LAN can win the race against the router.
// Any other network, including the fallback, keeps using DHCP.
void applyAddressing(bool mainNetwork) {
#if HAS_DEVELOPMENT_WIFI && defined(WIFI_STATIC_IP)
  if (mainNetwork) {
    IPAddress ip, gateway, subnet, dns;
    if (ip.fromString(WIFI_STATIC_IP) && gateway.fromString(WIFI_STATIC_GATEWAY) &&
        subnet.fromString(WIFI_STATIC_SUBNET) && dns.fromString(WIFI_STATIC_DNS)) {
      WiFi.config(ip, gateway, subnet, dns);
      return;
    }
  }
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
#else
  (void)mainNetwork;
#endif
}

// FNV-1a přes jméno, oddělovač a heslo.
uint32_t networkFingerprint(const String &ssid, const String &password) {
  uint32_t hash = 2166136261u;
  const auto mix = [&hash](const char *text) {
    for (; *text != '\0'; ++text) {
      hash ^= static_cast<uint8_t>(*text);
      hash *= 16777619u;
    }
    hash ^= 0xFFu;
    hash *= 16777619u;
  };
  mix(ssid.c_str());
  mix(password.c_str());
  return hash == 0 ? 1 : hash;
}

bool stationConfiguredFor(const String &ssid, const String &password) {
  return configuredNetwork != 0 &&
         configuredNetwork == networkFingerprint(ssid, password);
}

void beginWifiConnection(const String &ssid, const String &password) {
  if (stationConfiguredFor(ssid, password)) {
    WiFi.reconnect();
    return;
  }
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid.c_str(), password.c_str());
  configuredNetwork = networkFingerprint(ssid, password);
}

void connectStoredCredentials() {
  if (startupSsid.isEmpty()) return;
  const String &ssid = useFallback ? fallbackSsid : startupSsid;
  const String &password = useFallback ? fallbackPassword : startupPassword;
  // Adresování se mění jen s přechodem na jinou síť.
  if (!stationConfiguredFor(ssid, password)) applyAddressing(!useFallback);
  beginWifiConnection(ssid, password);
  connectingCandidate = startupCandidateActive && !useFallback;
  lastWifiAttempt = millis();
}

// The first connection after credentials were entered in the portal decides
// their fate: joining them makes them the stored network, joining the stored
// network instead drops them so the next startup does not wait on them again.
bool settleStartupCandidate() {
  if (connectingCandidate) {
    if (!saveStoredCredentials(startupSsid, startupPassword) ||
        !clearPendingCredentials()) {
      WiFi.disconnect();
      startupCandidateActive = false;
      connectingCandidate = false;
      startupSsid = storedSsid;
      startupPassword = storedPassword;
      return false;
    }
    storedSsid = startupSsid;
    storedPassword = startupPassword;
  } else {
    clearPendingCredentials();
    startupSsid = storedSsid;
    startupPassword = storedPassword;
  }
  startupCandidateActive = false;
  connectingCandidate = false;
  return true;
}
}  // namespace

void wifiProvisioningBegin() {
  // Arduino bere příznak persistent jen při inicializaci ovladače, tedy
  // v prvním WiFi.mode(). Nastavený až potom nechá ovladač ukládat konfiguraci
  // stanice do NVS ve flash při každém begin i automatickém znovupřipojení.
  // Přihlašovací údaje drží vlastní Preferences, konfigurace SDK stačí v RAM.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  configuredNetwork = 0;
  connectedOnce = false;
  normalStartReady = false;
  startupRetriesEnabled = true;
  startupCandidateActive = false;
  connectingCandidate = false;
  portalTryStored = true;
  startupSsid = "";
  startupPassword = "";
  loadStoredCredentials();
  useFallback = false;
  connectStoredCredentials();
}

void wifiProvisioningLoop() {
  if (provisioningActive) {
    if (WiFi.status() == WL_CONNECTED) {
      if (saveStoredCredentials(pendingSsid, pendingPassword)) {
        clearPendingCredentials();
        storedSsid = pendingSsid;
        storedPassword = pendingPassword;
        startupSsid = storedSsid;
        startupPassword = storedPassword;
        startupCandidateActive = false;
        connectingCandidate = false;
        normalStartReady = true;
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
      if (startupRetriesEnabled) connectStoredCredentials();
      return;
    }
    if (millis() - provisioningStartedAt >= PROVISIONING_TIMEOUT_MS) {
      WiFi.disconnect();
      provisioningActive = false;
      pendingSsid = "";
      pendingPassword = "";
      improvSerialServiceProvisioningFailed();
      if (startupRetriesEnabled) connectStoredCredentials();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (startupCandidateActive && !settleStartupCandidate()) return;
    normalStartReady = true;
    connectedOnce = true;
    // After a drop, auto-reconnect gets a full retry window on the network
    // that worked before the loop starts its own attempts again.
    lastWifiAttempt = millis();
    improvSerialServiceSetProvisioned();
    return;
  }
  normalStartReady = false;
  const uint32_t retryMs = connectedOnce ? WIFI_DROP_RETRY_MS : WIFI_RETRY_MS;
  if (startupRetriesEnabled && !startupSsid.isEmpty() &&
      millis() - lastWifiAttempt >= retryMs) {
    if (!fallbackSsid.isEmpty()) useFallback = !useFallback;
    connectStoredCredentials();
  }
}

void wifiProvisioningPauseStartupRetries() {
  startupRetriesEnabled = false;
  normalStartReady = false;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
}

bool wifiProvisioningPortalAttemptBegin() {
  if (startupSsid.isEmpty() || provisioningActive) return false;
  // With a failed candidate and a stored network, alternate between the two.
  const bool useStored =
      startupCandidateActive && !storedSsid.isEmpty() && portalTryStored;
  portalTryStored = !portalTryStored;
  if (useStored) {
    beginWifiConnection(storedSsid, storedPassword);
  } else {
    beginWifiConnection(startupSsid, startupPassword);
  }
  connectingCandidate = startupCandidateActive && !useStored;
  lastWifiAttempt = millis();
  return true;
}

void wifiProvisioningPortalAttemptEnd() {
  if (WiFi.status() != WL_CONNECTED && !provisioningActive) WiFi.disconnect();
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

bool wifiProvisioningSavePendingForRestart(const String &ssid,
                                           const String &password) {
#if FIRMWARE_RELEASE
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64)
    return false;
  return savePendingCredentials(ssid, password);
#else
  (void)ssid;
  (void)password;
  return false;
#endif
}

bool wifiProvisioningHasCredentials() { return !startupSsid.isEmpty(); }

bool wifiProvisioningIsActive() { return provisioningActive; }

bool wifiProvisioningReadyForNormalStart() { return normalStartReady; }
