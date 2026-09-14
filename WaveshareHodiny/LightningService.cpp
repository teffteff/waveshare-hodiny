#include "LightningService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <time.h>

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
constexpr time_t VALID_TIME_THRESHOLD = 1700000000;
// Server vrací nejvýš tisíc úderů, po ~60 bajtech.
constexpr size_t MAX_RESPONSE_BYTES = 96 * 1024;
// Silná bouřka nad celou republikou dá za půl hodiny i tisíce úderů; při
// přetečení se přepíšou nejstarší, které už radar skoro nekreslí.
constexpr size_t STROKE_CAPACITY = 2048;
// Čas mezi dotazy. S otevřeným radarem má být nový úder vidět brzy, jinak
// stačí hlídat výstrahu, na které minuta nehraje roli.
constexpr uint32_t VISIBLE_PERIOD_MS = 20000;
constexpr uint32_t BACKGROUND_PERIOD_MS = 60000;
constexpr uint32_t MAX_BACKOFF_MS = 5 * 60 * 1000;
constexpr uint32_t WAITING_RETRY_MS = 5000;
// Data starší než tři dotazy už o současném stavu nic neříkají.
constexpr uint32_t FRESHNESS_PERIODS = 3;
// Posun středu, od kterého je to jiný kruh a server se musí ptát znovu od
// začátku: údery z nové části kruhu jsou na serveru dávno přijaté.
constexpr float CIRCLE_MOVE_KM = 1.0f;

// Zapisuje tělo odpovědi do bufferu v PSRAM; nad strop se přestane přijímat.
// Stejná třída jako u radaru letadel a agendy - každá služba má svou kopii,
// aby změna u jedné nesáhla na ostatní.
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

struct Request {
  bool enabled = false;
  bool radarVisible = false;
  float alarmLatitude = 0.0f;
  float alarmLongitude = 0.0f;
  float alarmRadiusKm = 0.0f;
  float viewLatitude = 0.0f;
  float viewLongitude = 0.0f;
  float viewRadiusKm = 0.0f;
  char url[CLOCK_LIGHTNING_URL_LENGTH] = "";
};

TaskHandle_t taskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t strokeMutex = nullptr;
Request request;
uint32_t requestRevision = 0;
bool suspended = false;

LightningStroke *strokes = nullptr;
size_t strokeCount = 0;
size_t strokeWrite = 0;
uint32_t generation = 0;
uint8_t *responseBuffer = nullptr;

// Stav posledního dotazu; zapisuje úloha, čtou diagnostika a výstraha.
bool live = false;
uint32_t attempts = 0;
uint32_t successes = 0;
int lastHttpStatus = 0;
uint32_t lastDownloadedBytes = 0;
unsigned long lastSuccessAt = 0;
bool haveSuccess = false;
unsigned long nextFetchAt = 0;
uint32_t currentPeriodMs = BACKGROUND_PERIOD_MS;
uint32_t strokesReceived = 0;
float requestRadiusKm = 0.0f;
char statusMessage[64] = "Vypnuto";

void setStatus(const char *message) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(statusMessage, message, sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
}

bool ensureStorage() {
  if (strokes == nullptr) {
    strokes = static_cast<LightningStroke *>(heap_caps_calloc(
        STROKE_CAPACITY, sizeof(LightningStroke),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (responseBuffer == nullptr) {
    responseBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return strokes != nullptr && responseBuffer != nullptr;
}

void releaseStorage() {
  xSemaphoreTake(strokeMutex, portMAX_DELAY);
  if (strokes != nullptr) heap_caps_free(strokes);
  strokes = nullptr;
  strokeCount = 0;
  strokeWrite = 0;
  ++generation;
  xSemaphoreGive(strokeMutex);
  if (responseBuffer != nullptr) heap_caps_free(responseBuffer);
  responseBuffer = nullptr;
}

// Volá se se zamčeným strokeMutex z rozboru odpovědi.
void storeStroke(const LightningStroke &stroke, void *context) {
  auto *added = static_cast<uint32_t *>(context);
  // Opakování přijde po změně kruhu, kdy se server ptá od začátku. Id je na
  // serveru jedinečné, takže stačí porovnat je.
  if (stroke.id != 0) {
    for (size_t index = 0; index < strokeCount; ++index)
      if (strokes[index].id == stroke.id) return;
  }
  strokes[strokeWrite] = stroke;
  strokeWrite = (strokeWrite + 1) % STROKE_CAPACITY;
  if (strokeCount < STROKE_CAPACITY) ++strokeCount;
  ++*added;
}

// Jeden kruh pro dotaz, který pokryje výstrahu i pohled radaru.
void requestCircle(const Request &current, float &latitude, float &longitude,
                   float &radiusKm) {
  latitude = current.alarmLatitude;
  longitude = current.alarmLongitude;
  radiusKm = current.alarmRadiusKm;
  if (current.viewRadiusKm <= 0.0f) return;
  const float toAlarm =
      lightningDistanceKm(current.viewLatitude, current.viewLongitude,
                          current.alarmLatitude, current.alarmLongitude);
  latitude = current.viewLatitude;
  longitude = current.viewLongitude;
  radiusKm = std::max(current.viewRadiusKm, toAlarm + current.alarmRadiusKm);
}

// Stáhne odpověď do responseBuffer. Vrací počet bajtů, nebo -1.
long download(const char *url, int &httpStatus) {
  httpStatus = 0;
  long result = -1;
  const bool secure = strncmp(url, "https://", 8) == 0;
  {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (secure) {
      // Adresu serveru zadává uživatel, takže se ověřuje proti svazku kořenů
      // Mozilly, stejně jako u zpráv a agendy.
      secureClient.setCACertBundle(
          rootca_crt_bundle_start,
          static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
      secureClient.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    }
    WiFiClient &client = secure ? static_cast<WiFiClient &>(secureClient)
                                : plainClient;
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

// Vrací true při úspěchu; cursor se pak posune na čas serveru.
bool fetchStrokes(const char *baseUrl, float latitude, float longitude,
                  float radiusKm, double &cursor) {
  char url[CLOCK_LIGHTNING_URL_LENGTH + 96];
  if (!lightningFeedBuildUrl(baseUrl, latitude, longitude, radiusKm, cursor, url,
                             sizeof(url))) {
    setStatus("Adresa serveru blesků je příliš dlouhá");
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
  lastDownloadedBytes = length > 0 ? static_cast<uint32_t>(length) : 0;
  portEXIT_CRITICAL(&stateMux);
  if (length < 0) {
    setStatus(httpStatus == 401 ? "Server blesků odmítl heslo"
              : httpStatus > 0  ? "Server blesků odpověděl chybou"
                                : "Server blesků není dostupný");
    return false;
  }

  LightningMessageInfo info;
  uint32_t added = 0;
  xSemaphoreTake(strokeMutex, portMAX_DELAY);
  const LightningMessageKind kind = lightningParseMessage(
      reinterpret_cast<const char *>(responseBuffer),
      reinterpret_cast<const char *>(responseBuffer) + length, info,
      storeStroke, &added);
  if (added > 0) ++generation;
  xSemaphoreGive(strokeMutex);
  if (kind != LightningMessageKind::Strokes || info.serverTime <= 0.0) {
    setStatus("Neočekávaná odpověď serveru blesků");
    return false;
  }
  cursor = info.serverTime;
  portENTER_CRITICAL(&stateMux);
  ++successes;
  live = info.live;
  haveSuccess = true;
  lastSuccessAt = millis();
  strokesReceived += added;
  portEXIT_CRITICAL(&stateMux);
  setStatus(info.live ? "" : "Server se teprve připojuje k LightningMaps");
  return true;
}

void lightningTask(void *) {
  Request current;
  uint32_t lastRevision = UINT32_MAX;
  double cursor = 0.0;
  float lastLatitude = NAN;
  float lastLongitude = NAN;
  float lastRadiusKm = 0.0f;
  uint32_t failures = 0;
  char lastUrl[CLOCK_LIGHTNING_URL_LENGTH] = "";

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    portENTER_CRITICAL(&stateMux);
    current = request;
    const uint32_t revision = requestRevision;
    const bool stopped = suspended;
    portEXIT_CRITICAL(&stateMux);

    if (stopped || !current.enabled || current.url[0] == '\0') {
      if (strokes != nullptr) releaseStorage();
      cursor = 0.0;
      lastRadiusKm = 0.0f;
      lastUrl[0] = '\0';
      portENTER_CRITICAL(&stateMux);
      live = false;
      haveSuccess = false;
      portEXIT_CRITICAL(&stateMux);
      setStatus(current.enabled ? "Chybí adresa serveru blesků" : "Vypnuto");
      continue;
    }
    // Jiný server znamená jiná data: údery ani kurzor starého zdroje k novému
    // nepatří, a bez vyprázdnění by na radaru dál visely.
    if (strcmp(current.url, lastUrl) != 0) {
      if (strokes != nullptr) releaseStorage();
      strlcpy(lastUrl, current.url, sizeof(lastUrl));
      cursor = 0.0;
      lastRadiusKm = 0.0f;
      lastLatitude = NAN;
      portENTER_CRITICAL(&stateMux);
      live = false;
      haveSuccess = false;
      nextFetchAt = 0;
      portEXIT_CRITICAL(&stateMux);
    }
    if (!ensureStorage()) {
      setStatus("Pro blesky není dostatek PSRAM");
      continue;
    }
    if (WiFi.status() != WL_CONNECTED || time(nullptr) < VALID_TIME_THRESHOLD) {
      setStatus("Čekám na síť");
      continue;
    }

    float latitude = 0.0f;
    float longitude = 0.0f;
    float radiusKm = 0.0f;
    requestCircle(current, latitude, longitude, radiusKm);
    const uint32_t period =
        current.radarVisible ? VISIBLE_PERIOD_MS : BACKGROUND_PERIOD_MS;
    bool fetchNow = false;
    if (revision != lastRevision) {
      lastRevision = revision;
      // Jiný kruh znamená začít znovu: kurzor serveru platí jen pro údery,
      // o které si hodiny už jednou řekly.
      const bool moved =
          !std::isfinite(lastLatitude) ||
          lightningDistanceKm(latitude, longitude, lastLatitude,
                              lastLongitude) > CIRCLE_MOVE_KM ||
          radiusKm > lastRadiusKm;
      if (moved) {
        cursor = 0.0;
        fetchNow = true;
      }
      // Otevřený radar chce data hned, ne až za minutu.
      if (current.radarVisible && period < currentPeriodMs) fetchNow = true;
      lastLatitude = latitude;
      lastLongitude = longitude;
      lastRadiusKm = radiusKm;
    }
    portENTER_CRITICAL(&stateMux);
    currentPeriodMs = period;
    requestRadiusKm = radiusKm;
    const long untilDue = static_cast<long>(nextFetchAt - millis());
    portEXIT_CRITICAL(&stateMux);
    if (!fetchNow && untilDue > 0) continue;

    const bool ok = fetchStrokes(current.url, latitude, longitude, radiusKm,
                                 cursor);
    failures = ok ? 0 : failures + 1;
    uint32_t wait = period;
    if (!ok) {
      wait = period << (failures < 4 ? failures : 4);
      if (wait > MAX_BACKOFF_MS) wait = MAX_BACKOFF_MS;
    }
    portENTER_CRITICAL(&stateMux);
    // Server, který se k LightningMaps teprve připojuje, bude mít data za pár
    // vteřin; čekat na ně celou minutu nemá smysl.
    if (ok && !live) wait = WAITING_RETRY_MS;
    nextFetchAt = millis() + wait;
    portEXIT_CRITICAL(&stateMux);
  }
}
}  // namespace

void lightningServiceBegin() {
  if (taskHandle != nullptr) return;
  strokeMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCoreWithCaps(lightningTask, "lightning", 12288, nullptr, 1,
                                  &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void lightningServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  suspended = true;
  portEXIT_CRITICAL(&stateMux);
  if (taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void lightningServiceSetActive(bool enabled, const char *feedUrl,
                               float alarmLatitude, float alarmLongitude,
                               float alarmRadiusKm, float viewLatitude,
                               float viewLongitude, float viewRadiusKm,
                               bool radarVisible) {
  Request next;
  next.enabled = enabled;
  next.radarVisible = radarVisible;
  next.alarmLatitude = alarmLatitude;
  next.alarmLongitude = alarmLongitude;
  next.alarmRadiusKm = alarmRadiusKm;
  next.viewLatitude = viewLatitude;
  next.viewLongitude = viewLongitude;
  next.viewRadiusKm = viewRadiusKm;
  strlcpy(next.url, feedUrl != nullptr ? feedUrl : "", sizeof(next.url));
  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  changed = request.enabled != next.enabled ||
            request.radarVisible != next.radarVisible ||
            request.alarmLatitude != next.alarmLatitude ||
            request.alarmLongitude != next.alarmLongitude ||
            request.alarmRadiusKm != next.alarmRadiusKm ||
            request.viewLatitude != next.viewLatitude ||
            request.viewLongitude != next.viewLongitude ||
            request.viewRadiusKm != next.viewRadiusKm ||
            strcmp(request.url, next.url) != 0;
  if (changed) {
    request = next;
    ++requestRevision;
  }
  portEXIT_CRITICAL(&stateMux);
  if (changed && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

uint32_t lightningServiceGeneration() {
  if (strokeMutex == nullptr) return 0;
  xSemaphoreTake(strokeMutex, portMAX_DELAY);
  const uint32_t value = generation;
  xSemaphoreGive(strokeMutex);
  return value;
}

size_t lightningServiceCopyStrokes(LightningStroke *output, size_t capacity,
                                   uint32_t minEpochSeconds) {
  if (strokeMutex == nullptr || output == nullptr) return 0;
  size_t copied = 0;
  xSemaphoreTake(strokeMutex, portMAX_DELAY);
  for (size_t index = 0; strokes != nullptr && index < strokeCount &&
                         copied < capacity;
       ++index) {
    if (strokes[index].epochSeconds > minEpochSeconds)
      output[copied++] = strokes[index];
  }
  xSemaphoreGive(strokeMutex);
  return copied;
}

bool lightningServiceProximity(float latitude, float longitude, float radiusKm,
                               uint32_t maxAgeSeconds,
                               LightningProximity &proximity) {
  proximity = LightningProximity{};
  if (strokeMutex == nullptr) return false;
  portENTER_CRITICAL(&stateMux);
  const bool fresh =
      haveSuccess && live &&
      millis() - lastSuccessAt < currentPeriodMs * FRESHNESS_PERIODS +
                                     RESPONSE_TIMEOUT_MS;
  portEXIT_CRITICAL(&stateMux);
  const time_t now = time(nullptr);
  if (!fresh || now < VALID_TIME_THRESHOLD) return false;
  const uint32_t nowSeconds = static_cast<uint32_t>(now);
  xSemaphoreTake(strokeMutex, portMAX_DELAY);
  for (size_t index = 0; strokes != nullptr && index < strokeCount; ++index) {
    const LightningStroke &stroke = strokes[index];
    // Úder z budoucnosti je jen rozdíl hodin; bere se jako čerstvý.
    const uint32_t age = stroke.epochSeconds >= nowSeconds
                             ? 0
                             : nowSeconds - stroke.epochSeconds;
    if (age > maxAgeSeconds) continue;
    const float distance = lightningDistanceKm(latitude, longitude,
                                               stroke.latitude, stroke.longitude);
    if (distance > radiusKm) continue;
    if (proximity.count < UINT16_MAX) ++proximity.count;
    if (!(distance >= proximity.nearestKm)) proximity.nearestKm = distance;
    if (proximity.count == 1 || age < proximity.newestAgeSeconds)
      proximity.newestAgeSeconds = age;
  }
  xSemaphoreGive(strokeMutex);
  return true;
}

void lightningServiceDiagnostics(LightningDiagnostics &diagnostics) {
  const unsigned long now = millis();
  portENTER_CRITICAL(&stateMux);
  diagnostics.enabled = request.enabled && request.url[0] != '\0';
  diagnostics.live = live && haveSuccess;
  diagnostics.attempts = attempts;
  diagnostics.successes = successes;
  diagnostics.lastHttpStatus = lastHttpStatus;
  diagnostics.lastDownloadedBytes = lastDownloadedBytes;
  diagnostics.lastSuccessAgeMs = haveSuccess ? now - lastSuccessAt : 0;
  diagnostics.nextFetchInMs =
      static_cast<long>(nextFetchAt - now) > 0 ? nextFetchAt - now : 0;
  diagnostics.strokesReceived = strokesReceived;
  diagnostics.requestRadiusKm = requestRadiusKm;
  strlcpy(diagnostics.message, statusMessage, sizeof(diagnostics.message));
  portEXIT_CRITICAL(&stateMux);
  if (strokeMutex == nullptr) return;
  xSemaphoreTake(strokeMutex, portMAX_DELAY);
  diagnostics.bufferedStrokes = static_cast<uint16_t>(strokeCount);
  xSemaphoreGive(strokeMutex);
}
