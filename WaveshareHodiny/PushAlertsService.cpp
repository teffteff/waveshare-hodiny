#include "PushAlertsService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <freertos/idf_additions.h>

#include <cstring>

#include "HttpDownload.h"
#include "NetworkCoordinator.h"
#include "PushAlerts.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t NETWORK_GUARD_MS = 10000;
constexpr uint32_t FIRST_RETRY_MS = 60 * 1000;
constexpr uint32_t MAX_RETRY_MS = 30 * 60 * 1000;

struct Desired {
  bool valid = false;
  // Adresa tak, jak ji zadal majitel; /config/<MAC> se přidá až při odeslání,
  // protože hned po startu Wi-Fi ještě MAC hlásit nemusí.
  char url[CLOCK_PUSH_URL_LENGTH] = "";
  char body[PUSH_ALERTS_BODY_LENGTH] = "";
};

TaskHandle_t taskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
// Chtěné nastavení a revize; úloha si je pod zámkem zkopíruje.
Desired desired;
uint32_t desiredRevision = 0;
bool suspended = false;
char statusMessage[80] = "Vypnuto";

void setStatus(const char *message) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(statusMessage, message, sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
}

int putConfig(const Desired &request) {
  int status = 0;
  char id[PUSH_ALERTS_ID_LENGTH];
  char url[PUSH_ALERTS_REQUEST_URL_LENGTH];
  if (!pushAlertsIdFromMac(WiFi.macAddress().c_str(), id, sizeof(id)) ||
      strcmp(id, "000000000000") == 0 ||
      !pushAlertsBuildUrl(request.url, id, url, sizeof(url)))
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
      http.addHeader(F("Content-Type"), F("application/json"));
      status = http.sendRequest(
          "PUT",
          reinterpret_cast<uint8_t *>(const_cast<char *>(request.body)),
          strlen(request.body));
      http.end();
    }
    client.stop();
  }
  // Core připojuje svazek kořenů při každém spojení, ale nikdy ho neodpojí.
  if (secure) esp_crt_bundle_detach(nullptr);
  return status;
}

void describe(int status) {
  if (status == 200 || status == 204) {
    setStatus("Server nastavení přijal");
  } else if (status == 401 || status == 403) {
    setStatus("Server odmítl heslo v adrese");
  } else if (status == 404) {
    setStatus("Na adrese server upozornění není");
  } else if (status == 400) {
    setStatus("Server nastavení odmítl jako neplatné");
  } else if (status == 507) {
    setStatus("Na serveru je už příliš mnoho hodin");
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
  // Kopie v PSRAM zásobníku úlohy; dvakrát po sedmi stech bajtech.
  Desired current;
  Desired acknowledged;
  uint32_t failures = 0;
  uint32_t retryAt = 0;
  uint32_t lastRevision = UINT32_MAX;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
    portENTER_CRITICAL(&stateMux);
    current = desired;
    const uint32_t revision = desiredRevision;
    const bool stopped = suspended;
    portEXIT_CRITICAL(&stateMux);
    if (stopped || !current.valid) continue;

    // Nové nastavení se zkouší hned, i když staré zrovna čeká na opakování.
    if (revision != lastRevision) {
      lastRevision = revision;
      failures = 0;
      retryAt = 0;
    }
    if (strcmp(current.url, acknowledged.url) == 0 &&
        strcmp(current.body, acknowledged.body) == 0)
      continue;
    if (retryAt != 0 && static_cast<long>(millis() - retryAt) < 0) continue;
    if (WiFi.status() != WL_CONNECTED) {
      setStatus("Čeká na Wi-Fi");
      continue;
    }

    int status = 0;
    {
      NetworkOperationGuard guard(NETWORK_GUARD_MS);
      if (!guard) continue;
      status = putConfig(current);
    }
    describe(status);
    if (status == 200 || status == 204) {
      acknowledged = current;
      failures = 0;
      retryAt = 0;
      continue;
    }
    ++failures;
    uint32_t wait = FIRST_RETRY_MS << (failures < 6 ? failures - 1 : 5);
    if (wait > MAX_RETRY_MS) wait = MAX_RETRY_MS;
    retryAt = millis() + wait;
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
                                float latitude, float longitude,
                                const char *deviceName) {
  Desired next;
  // Bez adresy není komu co poslat. Server, který už hlídá, se o vymazané
  // adrese nedozví; vypnout se má přepínačem, adresa se maže až potom.
  strlcpy(next.url, alerts.url, sizeof(next.url));
  next.valid = alerts.url[0] != '\0' &&
               pushAlertsBuildBody(alerts, latitude, longitude, deviceName,
                                   next.body, sizeof(next.body));

  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  if (next.valid != desired.valid || strcmp(next.url, desired.url) != 0 ||
      strcmp(next.body, desired.body) != 0) {
    desired = next;
    ++desiredRevision;
    changed = true;
  }
  if (!next.valid)
    strlcpy(statusMessage, alerts.url[0] == '\0' ? "Vypnuto" : "Nastavení nejde sestavit",
            sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
  if (changed && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void pushAlertsServiceMessage(char *message, size_t capacity) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(message, statusMessage, capacity);
  portEXIT_CRITICAL(&stateMux);
}
