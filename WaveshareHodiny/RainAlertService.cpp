#include "RainAlertService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include <cstring>

#include "ClockConfig.h"
#include "HttpDownload.h"
#include "NetworkCoordinator.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
constexpr char USER_AGENT[] = "WaveshareHodiny";
constexpr uint32_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t NETWORK_GUARD_MS = 10000;
// Odpověď má kolem sta bajtů; kilobajt je strop s velkou rezervou, aby se
// chybová stránka cizího serveru nevešla do paměti hodin celá.
constexpr size_t MAX_RESPONSE_BYTES = 1024;
constexpr uint32_t MAX_BACKOFF_MS = 15 * 60 * 1000;
// Předpověď starší než tři dotazy už o současném stavu nic neříká.
constexpr uint32_t FRESHNESS_PERIODS = 3;

struct Request {
  bool enabled = false;
  float latitude = 0.0f;
  float longitude = 0.0f;
  uint8_t radiusKm = 5;
  uint8_t refreshMinutes = 5;
  char url[CLOCK_RAIN_URL_LENGTH] = "";
};

// Zapisuje tělo odpovědi do bufferu; nad strop se přestane přijímat. Stejná
// třída jako u blesků a agendy - každá služba má svou kopii, aby změna
// u jedné nesáhla na ostatní.
class BoundedBufferStream : public Stream {
 public:
  BoundedBufferStream(uint8_t *buffer, size_t capacity)
      : buffer_(buffer), capacity_(capacity) {}

  using Print::write;

  size_t write(uint8_t value) override { return write(&value, 1); }

  size_t write(const uint8_t *data, size_t size) override {
    if (data == nullptr || size == 0) return 0;
    const size_t remaining = capacity_ - length_;
    const size_t accepted = size < remaining ? size : remaining;
    if (accepted > 0) memcpy(buffer_ + length_, data, accepted);
    length_ += accepted;
    if (accepted != size) {
      overflowed_ = true;
      setWriteError();
    }
    return accepted;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  size_t length() const { return length_; }
  bool overflowed() const { return overflowed_; }

 private:
  uint8_t *buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t length_ = 0;
  bool overflowed_ = false;
};

TaskHandle_t taskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
Request request;
uint32_t requestRevision = 0;
bool suspended = false;

// Sdílený stav pod stateMux.
RainForecast storedForecast;
bool haveSuccess = false;
uint32_t lastSuccessAt = 0;
uint32_t generation = 0;
uint32_t attempts = 0;
uint32_t successes = 0;
int lastHttpStatus = 0;
uint32_t nextFetchAt = 0;
char statusMessage[64] = "Vypnuto";

uint8_t responseBuffer[MAX_RESPONSE_BYTES];

void setStatus(const char *message) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(statusMessage, message, sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
}

long download(const char *url, int &httpStatus) {
  long result = -1;
  httpStatus = 0;
  const bool secure = strncmp(url, "https://", 8) == 0;
  {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (secure) {
      // Adresu serveru zadává uživatel, takže se ověřuje proti svazku kořenů
      // Mozilly, stejně jako u zpráv, agendy a blesků.
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
    // Bez tohoto si HTTPClient::end() spojení schová a kontexty mbedTLS i
    // s buffery zůstanou alokované navždy.
    http.setReuse(false);
    http.setUserAgent(USER_AGENT);
    httpDownloadPrepare(http);
    if (http.begin(client, url)) {
      http.addHeader("Accept", "application/json");
      httpStatus = http.GET();
      if (httpStatus == HTTP_CODE_OK) {
        BoundedBufferStream response(responseBuffer, MAX_RESPONSE_BYTES - 1);
        // Ne writeToStream(): u chunked odpovědi se nemusí nikdy vrátit.
        const int bytesRead =
            httpDownloadBody(http, response, RESPONSE_TIMEOUT_MS);
        if (!response.overflowed() && bytesRead >= 0) {
          responseBuffer[response.length()] = '\0';
          result = static_cast<long>(response.length());
        }
      }
      http.end();
    }
    client.stop();
  }
  // Core připojuje svazek kořenů při každém spojení, ale nikdy ho neodpojí.
  if (secure) esp_crt_bundle_detach(nullptr);
  return result;
}

bool fetchForecast(const Request &current) {
  char url[CLOCK_RAIN_URL_LENGTH + 64];
  if (!rainFeedBuildUrl(current.url, current.latitude, current.longitude,
                        current.radiusKm, url, sizeof(url))) {
    setStatus("Adresa serveru srážek je příliš dlouhá");
    return false;
  }
  int httpStatus = 0;
  long length = -1;
  {
    NetworkOperationGuard guard(NETWORK_GUARD_MS);
    if (!guard) {
      setStatus("Síť je zaneprázdněná");
      return false;
    }
    length = download(url, httpStatus);
  }
  portENTER_CRITICAL(&stateMux);
  ++attempts;
  lastHttpStatus = httpStatus;
  portEXIT_CRITICAL(&stateMux);
  if (length < 0) {
    setStatus(httpStatus == 401 ? "Server srážek odmítl heslo"
              : httpStatus > 0  ? "Server srážek odpověděl chybou"
                                : "Server srážek není dostupný");
    return false;
  }

  RainForecast parsed;
  const char *text = reinterpret_cast<const char *>(responseBuffer);
  if (!rainForecastParse(text, text + length, parsed)) {
    setStatus("Neočekávaná odpověď serveru srážek");
    return false;
  }

  portENTER_CRITICAL(&stateMux);
  storedForecast = parsed;
  haveSuccess = true;
  lastSuccessAt = millis();
  ++successes;
  ++generation;
  portEXIT_CRITICAL(&stateMux);
  // Mimo dosah radaru se z prázdné předpovědi nesmí dělat závěr, a majitel
  // by měl vědět proč: jinak by jen viděl, že se nikdy nic nepřepne.
  setStatus(parsed.covered ? "" : "Poloha je mimo dosah českých radarů");
  return true;
}

void rainAlertTask(void *) {
  Request current;
  uint32_t lastRevision = UINT32_MAX;
  uint32_t failures = 0;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    portENTER_CRITICAL(&stateMux);
    current = request;
    const uint32_t revision = requestRevision;
    const bool stopped = suspended;
    portEXIT_CRITICAL(&stateMux);

    if (stopped || !current.enabled || current.url[0] == '\0') {
      portENTER_CRITICAL(&stateMux);
      haveSuccess = false;
      storedForecast = RainForecast{};
      portEXIT_CRITICAL(&stateMux);
      setStatus(current.enabled ? "Chybí adresa serveru srážek" : "Vypnuto");
      lastRevision = revision;
      continue;
    }
    // Změna adresy, polohy nebo poloměru znamená jinou odpověď; stará už
    // neplatí a na další dotaz se nečeká.
    if (revision != lastRevision) {
      lastRevision = revision;
      failures = 0;
      portENTER_CRITICAL(&stateMux);
      haveSuccess = false;
      storedForecast = RainForecast{};
      nextFetchAt = 0;
      portEXIT_CRITICAL(&stateMux);
    }
    if (WiFi.status() != WL_CONNECTED) {
      setStatus("Bez Wi-Fi");
      continue;
    }

    portENTER_CRITICAL(&stateMux);
    const uint32_t due = nextFetchAt;
    portEXIT_CRITICAL(&stateMux);
    if (due != 0 && static_cast<long>(millis() - due) < 0) continue;

    const bool ok = fetchForecast(current);
    const uint32_t period =
        static_cast<uint32_t>(current.refreshMinutes) * 60U * 1000U;
    uint32_t wait = period;
    if (ok) {
      failures = 0;
    } else {
      ++failures;
      wait = period << (failures < 4 ? failures : 4);
      if (wait > MAX_BACKOFF_MS) wait = MAX_BACKOFF_MS;
    }
    portENTER_CRITICAL(&stateMux);
    nextFetchAt = millis() + wait;
    portEXIT_CRITICAL(&stateMux);
  }
}

}  // namespace

void rainAlertServiceBegin() {
  if (taskHandle != nullptr) return;
  xTaskCreatePinnedToCoreWithCaps(rainAlertTask, "rain", 12288, nullptr, 1,
                                  &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void rainAlertServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  suspended = true;
  portEXIT_CRITICAL(&stateMux);
  if (taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void rainAlertServiceSetActive(bool enabled, const char *feedUrl,
                               float latitude, float longitude,
                               uint8_t radiusKm, uint8_t refreshMinutes) {
  Request next;
  next.enabled = enabled;
  next.latitude = latitude;
  next.longitude = longitude;
  next.radiusKm = radiusKm;
  next.refreshMinutes = refreshMinutes;
  if (feedUrl != nullptr) strlcpy(next.url, feedUrl, sizeof(next.url));

  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  if (request.enabled != next.enabled || request.radiusKm != next.radiusKm ||
      request.refreshMinutes != next.refreshMinutes ||
      request.latitude != next.latitude ||
      request.longitude != next.longitude ||
      strcmp(request.url, next.url) != 0) {
    request = next;
    ++requestRevision;
    changed = true;
  }
  portEXIT_CRITICAL(&stateMux);
  if (changed && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void rainAlertServiceStatus(RainAlertStatus &status) {
  status = RainAlertStatus{};
  const uint32_t now = millis();
  portENTER_CRITICAL(&stateMux);
  status.generation = generation;
  status.enabled = request.enabled && request.url[0] != '\0';
  status.attempts = attempts;
  status.successes = successes;
  status.lastHttpStatus = lastHttpStatus;
  status.lastSuccessAvailable = haveSuccess;
  status.lastSuccessAgeMs = haveSuccess ? now - lastSuccessAt : 0;
  status.nextFetchInMs =
      nextFetchAt != 0 && static_cast<long>(nextFetchAt - now) > 0
          ? nextFetchAt - now
          : 0;
  const uint32_t freshFor =
      static_cast<uint32_t>(request.refreshMinutes) * 60U * 1000U *
      FRESHNESS_PERIODS;
  // Stará předpověď se nevydává: obrazovka by podle ní přepínala na déšť,
  // který už dávno přešel.
  status.ready = haveSuccess && status.lastSuccessAgeMs <= freshFor;
  if (status.ready) status.forecast = storedForecast;
  strlcpy(status.message, statusMessage, sizeof(status.message));
  portEXIT_CRITICAL(&stateMux);
}
