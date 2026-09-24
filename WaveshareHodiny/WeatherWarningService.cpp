#include "WeatherWarningService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include <cstring>
#include <new>

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
// Šest výstrah je kolem kilobajtu; dva jsou strop s rezervou, aby se chybová
// stránka cizího serveru nevešla do paměti hodin celá.
constexpr size_t MAX_RESPONSE_BYTES = 2048;
constexpr uint32_t MAX_BACKOFF_MS = 15 * 60 * 1000;
// Výstrahy platí hodiny; šest nepovedených dotazů (hodina ve výchozím
// nastavení) se ještě dá přečkat se starším stavem.
constexpr uint32_t FRESHNESS_PERIODS = 6;

struct Request {
  bool enabled = false;
  float latitude = 0.0f;
  float longitude = 0.0f;
  bool english = false;
  uint8_t refreshMinutes = 10;
  char url[CLOCK_WARNINGS_URL_LENGTH] = "";
};

// Zapisuje tělo odpovědi do bufferu; nad strop se přestane přijímat. Stejná
// třída jako u deště a blesků - každá služba má svou kopii, aby změna
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
// Seznam výstrah má kolem 700 bajtů; leží v PSRAM, interní RAM patří TLS.
WeatherWarningFeed *storedFeed = nullptr;
bool haveSuccess = false;
uint32_t lastSuccessAt = 0;
uint32_t generation = 0;
uint32_t attempts = 0;
uint32_t successes = 0;
int lastHttpStatus = 0;
uint32_t nextFetchAt = 0;
char statusMessage[64] = "Vypnuto";

uint8_t *responseBuffer = nullptr;

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
      // Mozilly, stejně jako u deště, agendy a blesků.
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

// Síť držela jiná služba. To není chyba serveru: zkusí se to brzy znovu
// a do počítání chyb pro prodlužování pauzy se to nepočítá. Dřív se to
// počítalo a po uložení nastavení, kdy stahují všechny služby naráz, pak
// služba čekala dvojnásobek periody (výstrahy 20 minut, déšť 10).
constexpr uint32_t BUSY_RETRY_MS = 15000;
bool networkBusy = false;

bool fetchWarnings(const Request &current) {
  networkBusy = false;
  char url[CLOCK_WARNINGS_URL_LENGTH + 64];
  if (!weatherWarningsBuildUrl(current.url, current.latitude, current.longitude,
                               current.english, url, sizeof(url))) {
    setStatus("Adresa serveru výstrah je příliš dlouhá");
    return false;
  }
  int httpStatus = 0;
  long length = -1;
  {
    NetworkOperationGuard guard(NETWORK_GUARD_MS);
    networkBusy = !guard;
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
    setStatus(httpStatus == 401 ? "Server výstrah odmítl heslo"
              : httpStatus == 502 ? "Server výstrah nedostal data z ČHMÚ"
              : httpStatus > 0  ? "Server výstrah odpověděl chybou"
                                : "Server výstrah není dostupný");
    return false;
  }

  // Rozbor rovnou do sdíleného stavu by čtenáři mohl podstrčit napůl
  // vyplněný seznam; výsledek proto leží stranou (na zásobníku úlohy, tedy
  // v PSRAM) a přepíše se naráz.
  WeatherWarningFeed parsed;
  const char *text = reinterpret_cast<const char *>(responseBuffer);
  if (!weatherWarningsParse(text, text + length, parsed)) {
    setStatus("Neočekávaná odpověď serveru výstrah");
    return false;
  }

  portENTER_CRITICAL(&stateMux);
  *storedFeed = parsed;
  haveSuccess = true;
  lastSuccessAt = millis();
  ++successes;
  ++generation;
  portEXIT_CRITICAL(&stateMux);
  // Mimo Česko ČHMÚ výstrahy nevydává; prázdný seznam tam neznamená klid.
  setStatus(!parsed.covered ? "Poloha je mimo území Česka"
            : parsed.stale  ? "Server výstrah posílá starší stav"
                            : "");
  return true;
}

void weatherWarningTask(void *) {
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
      *storedFeed = WeatherWarningFeed{};
      portEXIT_CRITICAL(&stateMux);
      setStatus(current.enabled ? "Chybí adresa serveru výstrah" : "Vypnuto");
      lastRevision = revision;
      continue;
    }
    // Změna adresy, polohy nebo jazyka znamená jinou odpověď; stará už
    // neplatí a na další dotaz se nečeká.
    if (revision != lastRevision) {
      lastRevision = revision;
      failures = 0;
      portENTER_CRITICAL(&stateMux);
      haveSuccess = false;
      *storedFeed = WeatherWarningFeed{};
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

    const bool ok = fetchWarnings(current);
    const uint32_t period =
        static_cast<uint32_t>(current.refreshMinutes) * 60U * 1000U;
    uint32_t wait = period;
    if (ok) {
      failures = 0;
    } else if (networkBusy) {
      wait = BUSY_RETRY_MS;
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

void weatherWarningServiceBegin() {
  if (taskHandle != nullptr) return;
  if (storedFeed == nullptr) {
    void *memory = heap_caps_calloc(1, sizeof(WeatherWarningFeed),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) return;
    storedFeed = new (memory) WeatherWarningFeed();
  }
  if (responseBuffer == nullptr) {
    responseBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (responseBuffer == nullptr) return;
  }
  xTaskCreatePinnedToCoreWithCaps(weatherWarningTask, "warnings", 12288,
                                  nullptr, 1, &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void weatherWarningServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  suspended = true;
  portEXIT_CRITICAL(&stateMux);
  if (taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void weatherWarningServiceSetActive(bool enabled, const char *feedUrl,
                                    float latitude, float longitude,
                                    bool english, uint8_t refreshMinutes) {
  Request next;
  next.enabled = enabled;
  next.latitude = latitude;
  next.longitude = longitude;
  next.english = english;
  next.refreshMinutes = refreshMinutes;
  if (feedUrl != nullptr) strlcpy(next.url, feedUrl, sizeof(next.url));

  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  if (request.enabled != next.enabled || request.english != next.english ||
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

void weatherWarningServiceStatus(WeatherWarningStatus &status) {
  status = WeatherWarningStatus{};
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
  status.ready = haveSuccess && status.lastSuccessAgeMs <= freshFor;
  if (status.ready && storedFeed != nullptr) status.feed = *storedFeed;
  strlcpy(status.message, statusMessage, sizeof(status.message));
  portEXIT_CRITICAL(&stateMux);
}
