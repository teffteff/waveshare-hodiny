#include "PushAlertsService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <freertos/idf_additions.h>

#include <cstring>

#include "HttpDownload.h"
#include "NetworkCoordinator.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t NETWORK_GUARD_MS = 10000;
constexpr uint32_t FIRST_RETRY_MS = 60 * 1000;
constexpr uint32_t MAX_RETRY_MS = 30 * 60 * 1000;
// Nastavení se mění na webu serveru; hodiny se o změně dozví nejpozději takhle.
constexpr uint32_t REFRESH_MS = 30 * 60 * 1000;
constexpr uint32_t MANUAL_REFRESH_GAP_MS = 60 * 1000;
// Odpověď má kolem pěti set bajtů.
constexpr size_t MAX_RESPONSE_BYTES = 1024;

class BoundedPrint : public Print {
 public:
  BoundedPrint(char *buffer, size_t capacity)
      : buffer_(buffer), capacity_(capacity) {}
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t *data, size_t size) override {
    const size_t remaining = capacity_ - length_;
    const size_t accepted = size < remaining ? size : remaining;
    if (accepted > 0) memcpy(buffer_ + length_, data, accepted);
    length_ += accepted;
    return accepted;
  }
  size_t length() const { return length_; }

 private:
  char *buffer_;
  size_t capacity_;
  size_t length_ = 0;
};

struct Desired {
  bool valid = false;
  // Adresa tak, jak ji zadal majitel; /config/<MAC> se přidá až při dotazu,
  // protože hned po startu Wi-Fi ještě MAC hlásit nemusí.
  char url[CLOCK_PUSH_URL_LENGTH] = "";
  float latitude = 0.0f;
  float longitude = 0.0f;
};

TaskHandle_t taskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
// Chtěný stav a revize; úloha si je pod zámkem zkopíruje.
Desired desired;
uint32_t desiredRevision = 0;
bool suspended = false;
bool refreshRequested = false;
uint32_t lastManualRefreshMs = 0;
bool manualRefreshUsed = false;
char statusMessage[80] = "Vypnuto";
// Naposledy přečtené nastavení; pod stateMux.
PushAlertsServerSettings serverSettings;

void setStatus(const char *message) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(statusMessage, message, sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
}

int getSettings(const Desired &request, char *response, size_t capacity) {
  response[0] = '\0';
  int status = 0;
  char id[PUSH_ALERTS_ID_LENGTH];
  char url[PUSH_ALERTS_REQUEST_URL_LENGTH];
  if (!pushAlertsIdFromMac(WiFi.macAddress().c_str(), id, sizeof(id)) ||
      strcmp(id, "000000000000") == 0 ||
      !pushAlertsBuildUrl(request.url, id, request.latitude, request.longitude,
                          url, sizeof(url)))
    return 0;
  const bool secure = strncmp(url, "https://", 8) == 0;
  {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (secure) {
      secureClient.setCACertBundle(
          rootca_crt_bundle_start,
          static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
      secureClient.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    }
    WiFiClient &client =
        secure ? static_cast<WiFiClient &>(secureClient) : plainClient;
    HTTPClient http;
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(RESPONSE_TIMEOUT_MS);
    // Viz RainAlertService.cpp: bez toho zůstanou TLS buffery alokované.
    http.setReuse(false);
    // Přesměrování se nenásleduje: heslo z adresy by šlo i na cizí server.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent(F("WaveshareHodiny"));
    httpDownloadPrepare(http);
    if (http.begin(client, url)) {
      http.addHeader(F("Accept"), F("application/json"));
      status = http.GET();
      if (status == 200) {
        // Ne getString(): používá writeToStream, který se u chunked odpovědi
        // nemusí vrátit (viz HttpDownload.h).
        BoundedPrint body(response, capacity - 1);
        if (httpDownloadBody(http, body, RESPONSE_TIMEOUT_MS) >= 0)
          response[body.length()] = '\0';
      }
      http.end();
    }
    client.stop();
  }
  // Core připojuje svazek kořenů při každém spojení, ale nikdy ho neodpojí.
  if (secure) esp_crt_bundle_detach(nullptr);
  return status;
}

void describeFailure(int status) {
  if (status == 401 || status == 403) {
    setStatus("Server odmítl heslo v adrese");
  } else if (status == 404) {
    setStatus("Na adrese server upozornění není");
  } else if (status > 0) {
    char message[64];
    snprintf(message, sizeof(message), "Server odpověděl chybou HTTP %d",
             status);
    setStatus(message);
  } else {
    setStatus("Server upozornění není dostupný");
  }
}

void pushAlertsTask(void *) {
  // Odpověď v PSRAM zásobníku úlohy.
  char response[MAX_RESPONSE_BYTES];
  Desired current;
  uint32_t failures = 0;
  uint32_t nextAt = 0;
  bool scheduled = false;
  uint32_t lastRevision = UINT32_MAX;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
    portENTER_CRITICAL(&stateMux);
    current = desired;
    const uint32_t revision = desiredRevision;
    const bool stopped = suspended;
    const bool refresh = refreshRequested;
    refreshRequested = false;
    portEXIT_CRITICAL(&stateMux);
    if (stopped || !current.valid) continue;

    // Nová adresa nebo poloha se čte hned, i když stará čeká na opakování.
    if (revision != lastRevision || refresh) {
      if (revision != lastRevision) failures = 0;
      lastRevision = revision;
      scheduled = false;
    }
    if (scheduled && static_cast<long>(millis() - nextAt) < 0) continue;
    if (WiFi.status() != WL_CONNECTED) {
      setStatus("Čeká na Wi-Fi");
      continue;
    }

    int status = 0;
    {
      NetworkOperationGuard guard(NETWORK_GUARD_MS);
      if (!guard) continue;
      status = getSettings(current, response, sizeof(response));
    }
    PushAlertsServerSettings parsed;
    const bool readable =
        status == 200 &&
        pushAlertsParseAnswer(response, response + strlen(response), parsed);
    if (readable) {
      portENTER_CRITICAL(&stateMux);
      // Mezitím se mohla změnit adresa nebo poloha; pak by to nepatřilo sem.
      if (desiredRevision == revision) serverSettings = parsed;
      portEXIT_CRITICAL(&stateMux);
      setStatus(parsed.known ? "Nastavení je načtené"
                             : "Server zatím nezná polohu domu");
      failures = 0;
      nextAt = millis() + REFRESH_MS;
      scheduled = true;
      continue;
    }
    if (status == 200)
      setStatus("Server poslal nečitelnou odpověď");
    else
      describeFailure(status);
    ++failures;
    uint32_t wait = FIRST_RETRY_MS << (failures < 6 ? failures - 1 : 5);
    if (wait > MAX_RETRY_MS) wait = MAX_RETRY_MS;
    nextAt = millis() + wait;
    scheduled = true;
  }
}
}  // namespace

void pushAlertsServiceBegin() {
  if (taskHandle != nullptr) {
    portENTER_CRITICAL(&stateMux);
    suspended = false;
    portEXIT_CRITICAL(&stateMux);
    xTaskNotifyGive(taskHandle);
    return;
  }
  xTaskCreatePinnedToCoreWithCaps(pushAlertsTask, "push-alerts", 12288,
                                  nullptr, 1, &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void pushAlertsServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  suspended = true;
  portEXIT_CRITICAL(&stateMux);
}

void pushAlertsServiceSetConfig(const ClockPushAlertsConfig &alerts,
                                float latitude, float longitude) {
  Desired next;
  strlcpy(next.url, alerts.url, sizeof(next.url));
  next.latitude = latitude;
  next.longitude = longitude;
  next.valid = alerts.url[0] != '\0' && latitude >= -90.0f &&
               latitude <= 90.0f && longitude >= -180.0f && longitude <= 180.0f;

  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  const bool moved = next.latitude != desired.latitude ||
                     next.longitude != desired.longitude;
  if (next.valid != desired.valid || strcmp(next.url, desired.url) != 0 ||
      moved) {
    // Jiný server nebo poloha: staré nastavení i výška by tu lhaly.
    serverSettings = PushAlertsServerSettings();
    desired = next;
    ++desiredRevision;
    changed = true;
  }
  if (!next.valid)
    strlcpy(statusMessage, "Vypnuto", sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
  if (changed && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void pushAlertsServiceRefresh() {
  bool wake = false;
  portENTER_CRITICAL(&stateMux);
  const uint32_t now = millis();
  if (desired.valid &&
      (!manualRefreshUsed || now - lastManualRefreshMs >= MANUAL_REFRESH_GAP_MS)) {
    manualRefreshUsed = true;
    lastManualRefreshMs = now;
    refreshRequested = true;
    wake = true;
  }
  portEXIT_CRITICAL(&stateMux);
  if (wake && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void pushAlertsServiceLowPass(PushAlertsLowPass &lowPass) {
  portENTER_CRITICAL(&stateMux);
  lowPass.valid = serverSettings.known && serverSettings.low &&
                  serverSettings.elevationKnown;
  lowPass.groundElevationM = serverSettings.elevationM;
  lowPass.radiusM = serverSettings.lowDistanceM;
  lowPass.heightM = serverSettings.lowHeightM;
  portEXIT_CRITICAL(&stateMux);
}

void pushAlertsServiceSettings(PushAlertsServerSettings &settings) {
  portENTER_CRITICAL(&stateMux);
  settings = serverSettings;
  portEXIT_CRITICAL(&stateMux);
}

void pushAlertsServiceMessage(char *message, size_t capacity) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(message, statusMessage, capacity);
  portEXIT_CRITICAL(&stateMux);
}
