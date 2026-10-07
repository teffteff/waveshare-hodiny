#include "TrafficService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <string.h>
#include <time.h>

#include <new>

#include "ClockFonts.h"
#include "HttpDownload.h"
#include "MapLabelFont.h"
#include "NetworkCoordinator.h"
#include "SharedFrames.h"
#include "TrafficRender.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {

constexpr char USER_AGENT[] = "WaveshareHodiny";
constexpr uint32_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t NETWORK_GUARD_MS = 10000;
// Odpověď má dva až tři kilobajty; strop s rezervou na delší kolony a víc
// autobusů. Přes strop se zahazuje celá - useknutý JSON se rozebrat nedá.
constexpr size_t MAX_RESPONSE_BYTES = 16 * 1024;
constexpr time_t VALID_TIME_THRESHOLD = 1700000000;

constexpr uint32_t VISIBLE_PERIOD_MS = 60UL * 1000UL;
constexpr uint32_t HIDDEN_PERIOD_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t MAX_BACKOFF_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t NETWORK_RETRY_MS = 15000;

static_assert(TRAFFIC_FRAME_WIDTH == SHARED_FRAME_WIDTH &&
                  TRAFFIC_FRAME_HEIGHT == SHARED_FRAME_HEIGHT &&
                  TRAFFIC_FRAME_WIDTH == TRAFFIC_MAP_SIZE,
              "Mapa dopravy se kreslí do sdílených snímků");

// Data i buffer odpovědi leží v PSRAM; interní RAM patří TLS.
struct TrafficStorage {
  TrafficData live;
  TrafficData scratch;
};

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t taskHandle = nullptr;
TrafficStorage *storage = nullptr;
uint8_t *responseBuffer = nullptr;

// Požadavek z hlavní smyčky.
bool active = false;
bool visible = false;
bool redNightMode = false;
bool englishLabels = false;
char requestUrl[CLOCK_TRAFFIC_URL_LENGTH] = "";
uint32_t requestRevision = 0;
bool fetchNowRequested = false;
bool redrawRequested = false;

// Data a stav.
bool haveData = false;
bool loading = false;
uint32_t generation = 0;
uint32_t textsGeneration = 0;
unsigned long lastSuccessAt = 0;
unsigned long nextFetchAt = 0;
uint8_t failureCount = 0;
char statusMessage[64] = "";

// Snímky půjčené ze SharedFrames, stejně jako u radaru letadel: kreslí se do
// toho, který nedrží ani obrazovka, ani poslední zveřejněný snímek.
uint32_t frameLease = 0;
int displayedBuffer = -1;
int handedOutBuffer = -1;
bool frameReady = false;

// Jména míst českým písmem. Instance patří úloze dopravy (viz MapLabelFont.h).
MapLabelFont placeFont(&clock_czech_14, false);

void setStatusMessage(const char *text) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(statusMessage, text, sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
}

bool ensureStorage() {
  if (!sharedFramesReserve()) return false;
  if (storage == nullptr) {
    void *memory = heap_caps_malloc(sizeof(TrafficStorage),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) return false;
    storage = new (memory) TrafficStorage();
  }
  if (responseBuffer == nullptr) {
    responseBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return responseBuffer != nullptr;
}

void releaseStorage() {
  if (storage != nullptr) {
    storage->~TrafficStorage();
    heap_caps_free(storage);
    storage = nullptr;
  }
  if (responseBuffer != nullptr) {
    heap_caps_free(responseBuffer);
    responseBuffer = nullptr;
  }
}

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

// Jeden GET. Vrací délku těla, nebo -1; httpStatus nese kód odpovědi.
long download(const char *url, int &httpStatus, bool &tooLarge) {
  long result = -1;
  httpStatus = 0;
  tooLarge = false;
  const bool secure = strncmp(url, "https://", 8) == 0;
  {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (secure) {
      // Adresu zadává majitel, takže se ověřuje proti svazku kořenů Mozilly.
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
        const int bytesRead =
            httpDownloadBody(http, response, RESPONSE_TIMEOUT_MS);
        tooLarge = response.overflowed();
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

bool fetchTraffic(const char *url) {
  int httpStatus = 0;
  bool tooLarge = false;
  long length = -1;
  {
    NetworkOperationGuard guard(NETWORK_GUARD_MS);
    if (!guard) {
      setStatusMessage("Síť je zaneprázdněná");
      return false;
    }
    length = download(url, httpStatus, tooLarge);
  }
  if (length < 0) {
    char message[64];
    if (tooLarge)
      snprintf(message, sizeof(message), "Odpověď dopravy je příliš velká");
    else if (httpStatus == 401 || httpStatus == 403)
      snprintf(message, sizeof(message), "Server dopravy odmítl heslo");
    else if (httpStatus > 0)
      snprintf(message, sizeof(message), "Server dopravy vrátil %d", httpStatus);
    else
      snprintf(message, sizeof(message), "Server dopravy neodpovídá");
    setStatusMessage(message);
    return false;
  }
  // Rozbor do pracovní kopie; rozbitá odpověď tak platná data nesmaže.
  if (trafficFeedParse(reinterpret_cast<const char *>(responseBuffer),
                       static_cast<size_t>(length),
                       storage->scratch) != TrafficParseStatus::Ok) {
    setStatusMessage("Server dopravy poslal nečitelná data");
    return false;
  }
  return true;
}

// Prohodí pracovní kopii s živou. Snímek čte živá data pod zámkem, takže se
// živá kopie mění jen pod ním; prohazuje se po kusech, aby zámek nedržel
// přerušení vypnutá déle než pár mikrosekund.
void publishScratch() {
  // Čáry a body čte jen tahle úloha (kresba), takže se přepíšou bez zámku.
  TrafficData &live = storage->live;
  const TrafficData &scratch = storage->scratch;
  live.lineCount = scratch.lineCount;
  memcpy(live.lines, scratch.lines, sizeof(live.lines));
  live.placeCount = scratch.placeCount;
  memcpy(live.places, scratch.places, sizeof(live.places));
  live.exitCount = scratch.exitCount;
  memcpy(live.exits, scratch.exits, sizeof(live.exits));
  live.busCount = scratch.busCount;
  memcpy(live.buses, scratch.buses, sizeof(live.buses));
  // Texty čte i snímek z hlavní smyčky.
  portENTER_CRITICAL(&stateMux);
  live.now = scratch.now;
  live.sampleTime = scratch.sampleTime;
  live.stale = scratch.stale;
  live.driveCount = scratch.driveCount;
  memcpy(live.drives, scratch.drives, sizeof(live.drives));
  live.textCount = scratch.textCount;
  memcpy(live.texts, scratch.texts, sizeof(live.texts));
  live.warningCount = scratch.warningCount;
  memcpy(live.warnings, scratch.warnings, sizeof(live.warnings));
  live.departureCount = scratch.departureCount;
  memcpy(live.departures, scratch.departures, sizeof(live.departures));
  haveData = true;
  lastSuccessAt = millis();
  ++textsGeneration;
  ++generation;
  statusMessage[0] = '\0';
  portEXIT_CRITICAL(&stateMux);
}

void yieldToScheduler() { vTaskDelay(1); }

void drawFrame(bool night, bool english, uint32_t lease) {
  portENTER_CRITICAL(&stateMux);
  if (frameLease != lease) {
    frameLease = lease;
    displayedBuffer = -1;
    handedOutBuffer = -1;
  }
  int target = -1;
  for (int index = 0; index < static_cast<int>(SHARED_FRAME_COUNT); ++index) {
    if (index == displayedBuffer || index == handedOutBuffer) continue;
    target = index;
    break;
  }
  if (target < 0) redrawRequested = true;
  const bool withData = haveData;
  portEXIT_CRITICAL(&stateMux);
  if (target < 0) return;
  uint16_t *pixels = sharedFrame(target);
  if (pixels == nullptr) return;
  TrafficRenderResult result;
  trafficRender(pixels, withData ? &storage->live : nullptr, night, english,
                placeFont, yieldToScheduler, result);
  portENTER_CRITICAL(&stateMux);
  if (!sharedFramesLeaseValid(SharedFrameUser::Traffic, lease)) {
    redrawRequested = true;
    portEXIT_CRITICAL(&stateMux);
    return;
  }
  displayedBuffer = target;
  frameReady = true;
  ++generation;
  portEXIT_CRITICAL(&stateMux);
}

void renderFrame(bool night, bool english) {
  const uint32_t lease = sharedFramesBeginRender(SharedFrameUser::Traffic);
  if (lease == 0) {
    portENTER_CRITICAL(&stateMux);
    // Jiná obrazovka ještě dokresluje; zkusí se v dalším průchodu.
    if (visible) redrawRequested = true;
    portEXIT_CRITICAL(&stateMux);
    return;
  }
  drawFrame(night, english, lease);
  sharedFramesEndRender(SharedFrameUser::Traffic);
}

uint32_t backoffMs(uint8_t failures) {
  uint32_t delay = VISIBLE_PERIOD_MS;
  for (uint8_t step = 0; step < failures && delay < MAX_BACKOFF_MS; ++step)
    delay *= 2;
  return delay < MAX_BACKOFF_MS ? delay : MAX_BACKOFF_MS;
}

void trafficTask(void *) {
  char url[CLOCK_TRAFFIC_URL_LENGTH] = "";
  uint32_t lastRevision = UINT32_MAX;
  bool lastNight = false;
  bool lastEnglish = false;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    portENTER_CRITICAL(&stateMux);
    const bool wantActive = active;
    const bool wantVisible = visible;
    const bool night = redNightMode;
    const bool english = englishLabels;
    const uint32_t revision = requestRevision;
    strlcpy(url, requestUrl, sizeof(url));
    portEXIT_CRITICAL(&stateMux);
    if (!wantActive || url[0] == '\0') continue;
    if (!ensureStorage()) {
      setStatusMessage("Pro dopravu není dostatek PSRAM");
      continue;
    }

    portENTER_CRITICAL(&stateMux);
    bool redraw = redrawRequested;
    redrawRequested = false;
    bool fetchNow = fetchNowRequested;
    fetchNowRequested = false;
    const unsigned long dueAt = nextFetchAt;
    portEXIT_CRITICAL(&stateMux);
    if (revision != lastRevision) {
      // Nová adresa zahodí odklad po chybách staré adresy.
      lastRevision = revision;
      failureCount = 0;
      fetchNow = true;
    }
    if (night != lastNight || english != lastEnglish) redraw = true;
    lastNight = night;
    lastEnglish = english;

    // Viditelná obrazovka dostane mapu z toho, co je v paměti, hned - ještě
    // než odpoví server.
    if (wantVisible && redraw) {
      renderFrame(night, english);
      redraw = false;
    }

    if (WiFi.status() != WL_CONNECTED || time(nullptr) < VALID_TIME_THRESHOLD) {
      portENTER_CRITICAL(&stateMux);
      nextFetchAt = millis() + NETWORK_RETRY_MS;
      // Žádost o stažení počká na síť.
      fetchNowRequested = fetchNowRequested || fetchNow;
      portEXIT_CRITICAL(&stateMux);
      if (!haveData) setStatusMessage("Čekám na síť");
      continue;
    }
    if (!fetchNow && static_cast<long>(millis() - dueAt) < 0) continue;

    portENTER_CRITICAL(&stateMux);
    loading = true;
    portEXIT_CRITICAL(&stateMux);
    const bool ok = fetchTraffic(url);
    if (ok) {
      publishScratch();
      failureCount = 0;
    } else if (failureCount < 8) {
      ++failureCount;
    }
    portENTER_CRITICAL(&stateMux);
    loading = false;
    const uint32_t period = failureCount > 0 ? backoffMs(failureCount)
                            : visible        ? VISIBLE_PERIOD_MS
                                             : HIDDEN_PERIOD_MS;
    nextFetchAt = millis() + period;
    // Hláška chyby se zobrazí i beze změny dat.
    if (!ok) ++generation;
    const bool drawNow = visible || !frameReady;
    portEXIT_CRITICAL(&stateMux);
    // Schovaná obrazovka snímky nekreslí: patří tomu, kdo je na displeji.
    if (ok && drawNow && wantVisible) renderFrame(night, english);
  }
}

}  // namespace

void trafficServiceBegin() {
  if (taskHandle != nullptr) return;
  xTaskCreatePinnedToCoreWithCaps(trafficTask, "traffic", 16384, nullptr, 1,
                                  &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void trafficServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  active = false;
  visible = false;
  portEXIT_CRITICAL(&stateMux);
  if (taskHandle != nullptr) {
    vTaskDeleteWithCaps(taskHandle);
    taskHandle = nullptr;
  }
  sharedFramesEndRender(SharedFrameUser::Traffic);
  portENTER_CRITICAL(&stateMux);
  displayedBuffer = -1;
  handedOutBuffer = -1;
  frameReady = false;
  haveData = false;
  loading = false;
  ++generation;
  ++textsGeneration;
  portEXIT_CRITICAL(&stateMux);
  releaseStorage();
}

void trafficServiceSetActive(bool nowVisible, bool backgroundRefresh,
                             const char *url, bool english) {
  const char *wantedUrl = url != nullptr ? url : "";
  bool notify = false;
  portENTER_CRITICAL(&stateMux);
  const bool wasActive = active;
  const bool wasVisible = visible;
  if (strcmp(requestUrl, wantedUrl) != 0) {
    strlcpy(requestUrl, wantedUrl, sizeof(requestUrl));
    ++requestRevision;
    // Data ze staré adresy nepatří k nové.
    haveData = false;
    ++textsGeneration;
    ++generation;
    redrawRequested = true;
    notify = true;
  }
  if (englishLabels != english) {
    englishLabels = english;
    notify = true;
  }
  visible = nowVisible;
  active = (nowVisible || backgroundRefresh) && wantedUrl[0] != '\0';
  if (active && !wasActive) {
    fetchNowRequested = true;
    notify = true;
  }
  if (nowVisible && !wasVisible) {
    // Po otevření se mapa nakreslí hned z paměti a data starší než minuta se
    // stáhnou znovu.
    redrawRequested = true;
    if (!haveData ||
        static_cast<unsigned long>(millis() - lastSuccessAt) >= VISIBLE_PERIOD_MS)
      fetchNowRequested = true;
    notify = true;
  }
  if (!active) statusMessage[0] = '\0';
  portEXIT_CRITICAL(&stateMux);
  if (nowVisible && !wasVisible) sharedFramesClaim(SharedFrameUser::Traffic);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void trafficServiceSetRedNightMode(bool enabled) {
  portENTER_CRITICAL(&stateMux);
  if (redNightMode == enabled) {
    portEXIT_CRITICAL(&stateMux);
    return;
  }
  redNightMode = enabled;
  redrawRequested = visible;
  const bool notify = visible;
  // Popisky textové stránky se přebarví s další generací.
  ++generation;
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void trafficServiceSnapshot(TrafficSnapshot &snapshot) {
  portENTER_CRITICAL(&stateMux);
  snapshot.pixels =
      displayedBuffer >= 0 &&
              sharedFramesLeaseValid(SharedFrameUser::Traffic, frameLease)
          ? sharedFrame(displayedBuffer)
          : nullptr;
  // Obrazovka si ukazatel odnáší; do toho bufferu se teď nekreslí.
  if (snapshot.pixels != nullptr) handedOutBuffer = displayedBuffer;
  snapshot.generation = generation;
  snapshot.loading = loading || fetchNowRequested;
  snapshot.haveData = haveData;
  snapshot.lastSuccessAt = lastSuccessAt;
  strlcpy(snapshot.message, statusMessage, sizeof(snapshot.message));
  if (snapshot.textsGeneration != textsGeneration) {
    snapshot.textsGeneration = textsGeneration;
    TrafficTexts &texts = snapshot.texts;
    if (haveData && storage != nullptr) {
      const TrafficData &live = storage->live;
      texts.now = live.now;
      texts.sampleTime = live.sampleTime;
      texts.stale = live.stale;
      texts.driveCount = live.driveCount;
      memcpy(texts.drives, live.drives, sizeof(texts.drives));
      texts.textCount = live.textCount;
      memcpy(texts.texts, live.texts, sizeof(texts.texts));
      texts.warningCount = live.warningCount;
      memcpy(texts.warnings, live.warnings, sizeof(texts.warnings));
      texts.departureCount = live.departureCount;
      memcpy(texts.departures, live.departures, sizeof(texts.departures));
    } else {
      texts.now = 0;
      texts.sampleTime = 0;
      texts.stale = false;
      texts.driveCount = 0;
      texts.textCount = 0;
      texts.warningCount = 0;
      texts.departureCount = 0;
    }
  }
  portEXIT_CRITICAL(&stateMux);
}

bool trafficServiceHasData() {
  portENTER_CRITICAL(&stateMux);
  const bool result = haveData;
  portEXIT_CRITICAL(&stateMux);
  return result;
}
