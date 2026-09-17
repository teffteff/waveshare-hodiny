#include "SatelliteService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <sys/time.h>

#include <cmath>
#include <cstring>
#include <new>

#include "HttpDownload.h"
#include "MapCanvas.h"
#include "MapLabelFont.h"
#include "NetworkCoordinator.h"

// Adresu serveru zadává uživatel, takže se ověřuje proti svazku kořenů Mozilly,
// stejně jako u blesků a agendy.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {

constexpr char USER_AGENT[] =
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)";
constexpr uint32_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t NETWORK_GUARD_MS = 10000;
// Sto padesát družic po třinácti bodech je kolem 27 kB. Buffer leží v PSRAM
// a používá se dokola; přes strop se odpověď zahodí celá.
constexpr size_t MAX_RESPONSE_BYTES = 96 * 1024;
constexpr time_t VALID_TIME_THRESHOLD = 1700000000;

// Bez sítě nebo času se zkouší znovu po patnácti vteřinách.
constexpr uint32_t RETRY_INTERVAL_MS = 15000;
// Server stahuje skupinu z CelesTraku a odpověděl 503; hotovo bývá do minuty.
constexpr uint32_t SERVER_LOADING_RETRY_MS = 30000;
// Schovaná obrazovka ve střídání. Dráha pokrývá tři minuty od začátku
// patnáctivteřinového okna, takže obnova po 150 s nechá snímek platný, až na
// obrazovku přijde řada.
constexpr uint32_t HIDDEN_PERIOD_MS = 150000;
// Vidět se překresluje každou vteřinu: nízké družice se hýbou kolem pixelu za
// vteřinu, ISS v zenitu i o několik.
constexpr uint32_t RENDER_PERIOD_MS = 1000;
// Kolik stažení po sobě smí vybraná družice v datech chybět, než se detail
// zavře. Mezitím se čísla přiznají jako poslední známá.
constexpr uint8_t DETAIL_GRACE_FETCHES = 2;

constexpr int SKY_CENTER_X = SATELLITE_SKY_WIDTH / 2;
constexpr int SKY_CENTER_Y = SATELLITE_SKY_HEIGHT / 2;
// Obzor. Kruh displeje má poloměr 240; světové strany stojí uvnitř obzoru.
constexpr int SKY_RADIUS = 222;
constexpr float DEGREES_TO_RADIANS = 0.0174532925f;
// Pozorovatel je ve tmě, když je Slunce aspoň šest stupňů pod obzorem.
constexpr float DARK_SUN_ELEVATION = -6.0f;
// Budoucí dráha: dvě minuty dopředu po deseti vteřinách.
constexpr int TRACK_AHEAD_SECONDS = 120;
constexpr int TRACK_STEP_SECONDS = 10;

constexpr size_t PIXEL_COUNT =
    static_cast<size_t>(SATELLITE_SKY_WIDTH) * SATELLITE_SKY_HEIGHT;

constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xffff;
constexpr uint16_t COLOR_GRAY = 0x8410;
constexpr uint16_t COLOR_DARK_GRAY = 0x4208;
constexpr uint16_t COLOR_RING = 0x3186;
// Barvy skupin v pořadí SatelliteGroup. Webová stránka ukazuje tytéž.
constexpr uint16_t GROUP_COLORS[SATELLITE_GROUP_COUNT] = {
    0xffe0,  // stanice: žlutá
    0x5e9f,  // jasné: světle modrá
    0x6628,  // počasí: zelená
    0xc31f,  // navigace: fialová
    0xfd20,  // radioamatéři: oranžová
    0x9cf3,  // Starlink: šedá
};

// --- Stav -------------------------------------------------------------------
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t taskHandle = nullptr;

constexpr size_t DISPLAY_BUFFER_COUNT = 2;
uint16_t *displayBuffers[DISPLAY_BUFFER_COUNT] = {};
int displayedBuffer = -1;
int handedOutBuffer = -1;
uint16_t *pixels = nullptr;
SatelliteTrack *liveTracks = nullptr;
SatelliteTrack *scratchTracks = nullptr;
uint8_t *responseBuffer = nullptr;
MapLabelFont labelFont;

// Požadavek z obrazovky.
bool active = false;
bool visible = false;
bool redNightMode = false;
bool englishLabels = false;
float requestLatitude = 49.90461f;
float requestLongitude = 14.7842f;
ClockSatellitesConfig requestConfig;
uint32_t requestRevision = 0;

// Data.
SatelliteFeedInfo liveInfo;
size_t liveCount = 0;
bool haveTracks = false;
bool loading = false;
bool fetchNowRequested = true;
bool redrawRequested = false;
unsigned long lastSuccessAt = 0;
unsigned long nextFetchAt = 0;
int lastHttpStatus = 0;
size_t lastDownloadedBytes = 0;
char statusMessage[64] = "";

// Výsledek posledního kreslení.
uint32_t generation = 0;
bool dataFrameReady = false;
uint8_t shownCount = 0;
uint8_t visibleCount = 0;
bool observerDark = false;

// Výběr. Drží se katalogové číslo, ne index: pole se po každém stažení skládá
// znovu v jiném pořadí.
uint32_t selectedId = 0;
uint8_t selectionMissCount = 0;
SatelliteDetail selectionDetail;

struct SkyPoint {
  int16_t x = -32768;
  int16_t y = -32768;
  uint32_t noradId = 0;
};
// Body pro výběr klepnutím a výsledek zkoušky leží v PSRAM, alokované jednou při
// startu služby: v interní RAM by ubraly kilobajty, o které pak přijde TLS.
SkyPoint *skyPoints = nullptr;
uint8_t skyPointCount = 0;

// Zkouška adresy z webu.
enum class ProbeState : uint8_t { Idle, Pending, Running, Done };
ProbeState probeState = ProbeState::Idle;
bool probeAbandoned = false;
ClockSatellitesConfig probeConfig;
float probeLatitude = 0.0f;
float probeLongitude = 0.0f;
SatelliteProbeResult *probeResult = nullptr;

void setStatusMessage(const char *text) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(statusMessage, text, sizeof(statusMessage));
  portEXIT_CRITICAL(&stateMux);
}

bool ensureStorage() {
  for (uint16_t *&buffer : displayBuffers) {
    if (buffer != nullptr) continue;
    buffer = static_cast<uint16_t *>(heap_caps_malloc(
        PIXEL_COUNT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) return false;
    memset(buffer, 0, PIXEL_COUNT * sizeof(uint16_t));
  }
  if (liveTracks == nullptr) {
    liveTracks = static_cast<SatelliteTrack *>(
        heap_caps_calloc(SATELLITE_MAX_TRACKS, sizeof(SatelliteTrack),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (scratchTracks == nullptr) {
    scratchTracks = static_cast<SatelliteTrack *>(
        heap_caps_calloc(SATELLITE_MAX_TRACKS, sizeof(SatelliteTrack),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (responseBuffer == nullptr) {
    responseBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return liveTracks != nullptr && scratchTracks != nullptr &&
         responseBuffer != nullptr;
}

void releaseStorage() {
  for (uint16_t *&buffer : displayBuffers) {
    if (buffer == nullptr) continue;
    heap_caps_free(buffer);
    buffer = nullptr;
  }
  pixels = nullptr;
  for (SatelliteTrack **list : {&liveTracks, &scratchTracks}) {
    if (*list == nullptr) continue;
    heap_caps_free(*list);
    *list = nullptr;
  }
  if (responseBuffer != nullptr) {
    heap_caps_free(responseBuffer);
    responseBuffer = nullptr;
  }
}

// Unixový čas se zlomkem sekundy. Dráha se mezi body interpoluje, takže celé
// vteřiny by družice posouvaly skokem.
bool currentEpoch(double &epoch) {
  struct timeval now;
  if (gettimeofday(&now, nullptr) != 0 || now.tv_sec < VALID_TIME_THRESHOLD)
    return false;
  epoch = static_cast<double>(now.tv_sec) + now.tv_usec / 1e6;
  return true;
}

bool feedCoversEpoch(const SatelliteFeedInfo &info, double epoch) {
  return info.sampleCount >= 2 && epoch >= info.startEpoch &&
         epoch <= satelliteFeedEndEpoch(info);
}

// Zapisuje tělo odpovědi do bufferu v PSRAM; přes strop přestane přijímat.
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

// Stáhne odpověď do responseBuffer. Vrací počet bajtů, nebo -1.
long download(const char *url, int &httpStatus) {
  httpStatus = 0;
  long result = -1;
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

// Hláška k nepovedenému stažení. Hodiny s heslem v http:// adrese web vůbec
// neuloží, takže tu na to kontrola není.
const char *downloadFailureText(int httpStatus, long length) {
  if (httpStatus == 401 || httpStatus == 403) return "Server družic odmítl heslo";
  if (httpStatus == 404) return "Na adrese serveru družice nejsou";
  if (httpStatus == 503) return "Server stahuje dráhy družic";
  if (httpStatus == HTTP_CODE_OK && length < 0)
    return "Odpověď serveru družic je příliš velká";
  if (httpStatus > 0) return "Server družic odpověděl chybou";
  return "Server družic není dostupný";
}

// Stáhne a rozebere odpověď do scratchTracks. Vrací počet bajtů nebo -1;
// status a info vyplní v obou případech.
long fetchInto(const ClockSatellitesConfig &config, float latitude,
               float longitude, int &httpStatus, SatelliteFeedInfo &info,
               SatelliteParseStatus &parseStatus, bool &urlTooLong) {
  urlTooLong = false;
  parseStatus = SatelliteParseStatus::Invalid;
  httpStatus = 0;
  char url[CLOCK_SATELLITES_URL_LENGTH + 96];
  if (!satelliteFeedBuildUrl(config.url, latitude, longitude, config.groups,
                             config.minElevationDeg, url, sizeof(url))) {
    urlTooLong = true;
    return -1;
  }
  long length = -1;
  {
    NetworkOperationGuard guard(NETWORK_GUARD_MS);
    if (!guard) return -2;
    length = download(url, httpStatus);
  }
  if (length < 0) return length;
  const char *text = reinterpret_cast<const char *>(responseBuffer);
  parseStatus = satelliteParseFeed(text, text + length, scratchTracks,
                                   SATELLITE_MAX_TRACKS, info);
  return length;
}

bool fetchTracks(const ClockSatellitesConfig &config, float latitude,
                 float longitude, uint32_t &retryMs) {
  retryMs = 0;
  int httpStatus = 0;
  SatelliteFeedInfo info;
  SatelliteParseStatus parseStatus = SatelliteParseStatus::Invalid;
  bool urlTooLong = false;
  const long length = fetchInto(config, latitude, longitude, httpStatus, info,
                                parseStatus, urlTooLong);
  if (urlTooLong) {
    setStatusMessage("Adresa serveru družic je příliš dlouhá");
    return false;
  }
  if (length == -2) {
    setStatusMessage("Síť je zaneprázdněná");
    retryMs = RETRY_INTERVAL_MS;
    return false;
  }
  portENTER_CRITICAL(&stateMux);
  lastHttpStatus = httpStatus;
  lastDownloadedBytes = length > 0 ? static_cast<size_t>(length) : 0;
  portEXIT_CRITICAL(&stateMux);
  if (length < 0) {
    // Dosavadní dráhy zůstávají: pokrývají ještě až tři minuty a obrazovka po
    // jednom nepovedeném stažení nemá zhasnout.
    setStatusMessage(downloadFailureText(httpStatus, length));
    if (httpStatus == 503) retryMs = SERVER_LOADING_RETRY_MS;
    return false;
  }
  if (parseStatus != SatelliteParseStatus::Ok) {
    setStatusMessage(parseStatus == SatelliteParseStatus::NotJson
                         ? "Neočekávaná odpověď serveru družic"
                         : "Server družic poslal jiný formát");
    return false;
  }

  // Hotový rozbor se překlopí naráz prohozením ukazatelů; výběr klepnutím čte
  // seznam z jiné úlohy a nesmí chytit družici rozepsanou do půlky.
  portENTER_CRITICAL(&stateMux);
  SatelliteTrack *parsed = scratchTracks;
  scratchTracks = liveTracks;
  liveTracks = parsed;
  liveCount = info.count;
  liveInfo = info;
  haveTracks = true;
  lastSuccessAt = millis();
  if (selectedId != 0) {
    bool found = false;
    for (size_t index = 0; index < liveCount; ++index) {
      if (liveTracks[index].noradId == selectedId) {
        found = true;
        break;
      }
    }
    if (found) {
      selectionMissCount = 0;
    } else if (++selectionMissCount > DETAIL_GRACE_FETCHES) {
      selectedId = 0;
      selectionMissCount = 0;
    }
  }
  portEXIT_CRITICAL(&stateMux);
  if (info.problem[0] != '\0') {
    setStatusMessage("Některé dráhy družic jsou staré");
  } else if (info.pending) {
    setStatusMessage("Server ještě stahuje část drah");
    retryMs = SERVER_LOADING_RETRY_MS;
  } else {
    setStatusMessage("");
  }
  return true;
}

// --- Kreslení ---------------------------------------------------------------
bool nightPalette = false;

uint16_t paletteColor(uint16_t color) {
  if (!nightPalette) return color;
  const uint16_t red = ((color >> 11) & 0x1f) << 3;
  const uint16_t green = ((color >> 5) & 0x3f) << 2;
  const uint16_t blue = (color & 0x1f) << 3;
  const uint16_t luma = (77 * red + 150 * green + 29 * blue) >> 8;
  return static_cast<uint16_t>((luma >> 3) << 11);
}

struct Rotation {
  float sine = 0.0f;
  float cosine = 1.0f;
};

// Bod kruhu (sever nahoře) na displej s azimutem `topBearing` nahoře. Hodnoty
// jsou z interpolace čísel od serveru; nesmyslné se ořízne dřív, než se
// převede na int.
void toScreen(const Rotation &rotation, float x, float y, int &screenX,
              int &screenY) {
  const float rotatedX = x * rotation.cosine + y * rotation.sine;
  const float rotatedY = y * rotation.cosine - x * rotation.sine;
  const auto toPixel = [](float value) {
    if (!std::isfinite(value)) return 0;
    if (value > 4.0f) value = 4.0f;
    if (value < -4.0f) value = -4.0f;
    return static_cast<int>(std::lround(value * SKY_RADIUS));
  };
  screenX = SKY_CENTER_X + toPixel(rotatedX);
  screenY = SKY_CENTER_Y + toPixel(rotatedY);
}

void drawDottedCircle(int radius, uint16_t color) {
  // Tečka každé tři stupně; plná kružnice by se pletla s obzorem.
  for (int degree = 0; degree < 360; degree += 3) {
    const float angle = degree * DEGREES_TO_RADIANS;
    setMapPixel(pixels,
                SKY_CENTER_X + static_cast<int>(std::lround(radius * std::sin(angle))),
                SKY_CENTER_Y - static_cast<int>(std::lround(radius * std::cos(angle))),
                color, 100);
  }
}

void drawSkyGrid(const Rotation &rotation, uint16_t topBearing,
                 uint8_t minElevation, bool english) {
  const uint16_t ring = paletteColor(COLOR_RING);
  drawMapCircle(pixels, SKY_CENTER_X, SKY_CENTER_Y, SKY_RADIUS,
                paletteColor(COLOR_DARK_GRAY), 100);
  drawMapCircle(pixels, SKY_CENTER_X, SKY_CENTER_Y, SKY_RADIUS * 2 / 3, ring,
                100);
  drawMapCircle(pixels, SKY_CENTER_X, SKY_CENTER_Y, SKY_RADIUS / 3, ring, 100);
  // Osy sever-jih a východ-západ, otočené s oblohou.
  for (int bearing = 0; bearing < 180; bearing += 90) {
    float x = 0.0f;
    float y = 0.0f;
    satelliteSkyProject(static_cast<float>(bearing), 0.0f, x, y);
    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    toScreen(rotation, x, y, ax, ay);
    toScreen(rotation, -x, -y, bx, by);
    drawMapLine(pixels, ax, ay, bx, by, ring, 100);
  }
  if (minElevation > 0) {
    drawDottedCircle(SKY_RADIUS * (90 - minElevation) / 90,
                     paletteColor(COLOR_GRAY));
  }
  for (int offset = -5; offset <= 5; ++offset) {
    setMapPixel(pixels, SKY_CENTER_X + offset, SKY_CENTER_Y,
                paletteColor(COLOR_GRAY), 100);
    setMapPixel(pixels, SKY_CENTER_X, SKY_CENTER_Y + offset,
                paletteColor(COLOR_GRAY), 100);
  }
  // Světové strany uvnitř obzoru.
  static const char *const CZECH[4] = {"S", "V", "J", "Z"};
  static const char *const ENGLISH[4] = {"N", "E", "S", "W"};
  const char *const *labels = english ? ENGLISH : CZECH;
  constexpr int MARK_RADIUS = SKY_RADIUS - 12;
  for (int index = 0; index < 4; ++index) {
    const float angle =
        (index * 90 - static_cast<int>(topBearing)) * DEGREES_TO_RADIANS;
    const int x = SKY_CENTER_X +
                  static_cast<int>(std::lround(MARK_RADIUS * std::sin(angle))) - 2;
    const int y = SKY_CENTER_Y -
                  static_cast<int>(std::lround(MARK_RADIUS * std::cos(angle))) - 3;
    drawMapText(pixels, x, y, labels[index], paletteColor(COLOR_GRAY), 100);
  }
  // Popisky výšek kruhů pod zenitem, u jižní poloviny osy.
  drawMapText(pixels, SKY_CENTER_X + 4, SKY_CENTER_Y + SKY_RADIUS / 3 - 9, "60",
              paletteColor(COLOR_DARK_GRAY), 100);
  drawMapText(pixels, SKY_CENTER_X + 4, SKY_CENTER_Y + SKY_RADIUS * 2 / 3 - 9,
              "30", paletteColor(COLOR_DARK_GRAY), 100);
}

// Pásma, která si drží LVGL popisky obrazovky; popisky družic musí prohrát.
void reserveChromeBands(MapLabelPlacer &placer) {
  const MapLabelBox bands[] = {
      {0, 18, SATELLITE_SKY_WIDTH, 22},   // tečky obrazovek
      {0, 40, SATELLITE_SKY_WIDTH, 28},   // čas
      {0, 69, SATELLITE_SKY_WIDTH, 26},   // počet družic
      {0, 406, SATELLITE_SKY_WIDTH, 30},  // přelet ISS
  };
  for (const MapLabelBox &band : bands) placer.claim(band);
}

struct PlannedSatellite {
  uint8_t index;
  int16_t x;
  int16_t y;
  int16_t labelX;
  int16_t labelY;
  bool labeled;
  bool visibleNow;
  SatelliteSkyPoint point;
};

void renderFrame(const ClockSatellitesConfig &config, bool night, bool english,
                 double epoch, bool haveEpoch) {
  if (displayBuffers[0] == nullptr || displayBuffers[1] == nullptr ||
      liveTracks == nullptr || skyPoints == nullptr)
    return;
  portENTER_CRITICAL(&stateMux);
  int target = -1;
  for (int index = 0; index < static_cast<int>(DISPLAY_BUFFER_COUNT); ++index) {
    if (index == displayedBuffer || index == handedOutBuffer) continue;
    target = index;
    break;
  }
  if (target < 0) redrawRequested = true;
  // Seznam mění jen tahle úloha, takže se čte bez zámku; výběr ne.
  const uint32_t selection = selectedId;
  const size_t count = liveCount;
  const SatelliteFeedInfo info = liveInfo;
  const bool tracksKnown = haveTracks;
  portEXIT_CRITICAL(&stateMux);
  if (target < 0) return;
  pixels = displayBuffers[target];
  nightPalette = night;

  Rotation rotation;
  const float topRadians = config.topBearingDeg * DEGREES_TO_RADIANS;
  rotation.sine = std::sin(topRadians);
  rotation.cosine = std::cos(topRadians);

  const uint16_t background = paletteColor(COLOR_BLACK);
  for (size_t index = 0; index < PIXEL_COUNT; ++index) pixels[index] = background;
  drawSkyGrid(rotation, config.topBearingDeg, config.minElevationDeg, english);

  const bool dataCurrent =
      tracksKnown && haveEpoch && feedCoversEpoch(info, epoch);
  const bool dark = dataCurrent && info.hasSunElevation &&
                    info.sunElevationDeg <= DARK_SUN_ELEVATION;

  MapLabelPlacer placer;
  reserveChromeBands(placer);
  // Na zásobníku úlohy (PSRAM), ne ve statické paměti v interní RAM.
  PlannedSatellite plan[SATELLITE_MAX_TRACKS];
  uint8_t planCount = 0;
  uint8_t visibleNow = 0;
  int selectedPlan = -1;
  SatelliteDetail detail;

  if (dataCurrent) {
    for (size_t index = 0; index < count; ++index) {
      const SatelliteTrack &track = liveTracks[index];
      SatelliteSkyPoint point;
      if (!satelliteTrackAt(track, info, epoch, point)) continue;
      const bool selected = track.noradId == selection;
      if (selected) {
        detail.open = true;
        strlcpy(detail.name, track.name, sizeof(detail.name));
        detail.noradId = track.noradId;
        detail.group = track.group;
        detail.azimuthDeg = point.azimuthDeg;
        detail.elevationDeg = point.elevationDeg;
        detail.altitudeKm = track.altitudeKm;
        detail.rangeKm = track.rangeKm;
        detail.sunlit = track.sunlit;
        detail.visibleNow = dark && track.sunlit && point.elevationDeg >= 0.0f;
        detail.lost = point.elevationDeg < config.minElevationDeg;
      }
      if (point.elevationDeg < config.minElevationDeg) continue;
      int x = 0;
      int y = 0;
      toScreen(rotation, point.x, point.y, x, y);
      PlannedSatellite &entry = plan[planCount++];
      entry.index = static_cast<uint8_t>(index);
      entry.x = static_cast<int16_t>(x);
      entry.y = static_cast<int16_t>(y);
      entry.labeled = false;
      entry.visibleNow = dark && track.sunlit;
      entry.point = point;
      if (entry.visibleNow) ++visibleNow;
      if (selected) selectedPlan = planCount - 1;
    }

    // Popisky: nejdřív vybraná družice a stanice, pak ty, které jde vidět,
    // nakonec ostatní kromě Starlinku - těch je tolik, že by popisky zakryly
    // oblohu.
    char label[SATELLITE_NAME_LENGTH];
    for (int round = 0; round < 3; ++round) {
      for (uint8_t planIndex = 0; planIndex < planCount; ++planIndex) {
        PlannedSatellite &entry = plan[planIndex];
        const SatelliteTrack &track = liveTracks[entry.index];
        const bool priority = static_cast<int>(planIndex) == selectedPlan ||
                              track.group == SATELLITE_GROUP_STATIONS;
        const int wanted = priority ? 0 : (entry.visibleNow ? 1 : 2);
        if (wanted != round || entry.labeled) continue;
        if (round == 2 && track.group == SATELLITE_GROUP_STARLINK) continue;
        satelliteShortName(track.name, label, sizeof(label));
        if (label[0] == '\0') continue;
        MapLabelBox box;
        if (!mapPlaceIconLabel(placer, entry.x, entry.y, labelFont.width(label),
                               box, labelFont.labelHeight()))
          continue;
        entry.labeled = true;
        entry.labelX = static_cast<int16_t>(box.x);
        entry.labelY = static_cast<int16_t>(box.y);
      }
    }

    // Budoucí dráha pod značkami, aby čára nepřekreslila sousední družici.
    if (config.showTracks) {
      for (uint8_t planIndex = 0; planIndex < planCount; ++planIndex) {
        const PlannedSatellite &entry = plan[planIndex];
        const SatelliteTrack &track = liveTracks[entry.index];
        const bool selected = static_cast<int>(planIndex) == selectedPlan;
        if (track.group == SATELLITE_GROUP_STARLINK && !selected) continue;
        const uint16_t color = paletteColor(GROUP_COLORS[track.group]);
        int previousX = entry.x;
        int previousY = entry.y;
        for (int ahead = TRACK_STEP_SECONDS; ahead <= TRACK_AHEAD_SECONDS;
             ahead += TRACK_STEP_SECONDS) {
          SatelliteSkyPoint future;
          if (!satelliteTrackAt(track, info, epoch + ahead, future)) break;
          if (future.elevationDeg < 0.0f) break;
          int x = 0;
          int y = 0;
          toScreen(rotation, future.x, future.y, x, y);
          drawMapLine(pixels, previousX, previousY, x, y, color,
                      selected ? 70 : 35);
          previousX = x;
          previousY = y;
        }
      }
    }

    for (uint8_t planIndex = 0; planIndex < planCount; ++planIndex) {
      const PlannedSatellite &entry = plan[planIndex];
      const SatelliteTrack &track = liveTracks[entry.index];
      const uint16_t color = paletteColor(GROUP_COLORS[track.group]);
      const bool selected = static_cast<int>(planIndex) == selectedPlan;
      const int radius = track.group == SATELLITE_GROUP_STATIONS ? 5
                         : track.group == SATELLITE_GROUP_STARLINK ? 1
                                                                   : 3;
      if (selected)
        drawMapCircle(pixels, entry.x, entry.y, radius + 8,
                      paletteColor(COLOR_WHITE), 100);
      if (entry.visibleNow) {
        // Jde vidět okem: plná tečka se svatozáří.
        drawMapCircle(pixels, entry.x, entry.y, radius + 3, color, 45);
        fillMapCircle(pixels, entry.x, entry.y, radius, color, 100);
      } else if (track.sunlit) {
        fillMapCircle(pixels, entry.x, entry.y, radius, color, 80);
      } else {
        // Ve stínu Země: jen obrys.
        if (radius <= 1)
          fillMapCircle(pixels, entry.x, entry.y, radius, color, 40);
        else
          drawMapCircle(pixels, entry.x, entry.y, radius, color, 70);
      }
      if (entry.labeled) {
        char label[SATELLITE_NAME_LENGTH];
        satelliteShortName(track.name, label, sizeof(label));
        labelFont.draw(pixels, entry.labelX + 2, entry.labelY + 3, label,
                       paletteColor(selected ? COLOR_WHITE : color),
                       entry.visibleNow || selected ? 100 : 75);
      }
    }
  }

  portENTER_CRITICAL(&stateMux);
  skyPointCount = 0;
  for (uint8_t planIndex = 0; planIndex < planCount; ++planIndex) {
    skyPoints[skyPointCount].x = plan[planIndex].x;
    skyPoints[skyPointCount].y = plan[planIndex].y;
    skyPoints[skyPointCount].noradId = liveTracks[plan[planIndex].index].noradId;
    ++skyPointCount;
  }
  shownCount = planCount;
  visibleCount = visibleNow;
  observerDark = dark;
  if (selectedId != 0 && selectedId == selection) {
    if (detail.open) {
      selectionDetail = detail;
    } else if (selectionDetail.noradId == selection) {
      // Družice v aktuálních datech není; zůstanou poslední čísla.
      selectionDetail.lost = true;
    }
  }
  displayedBuffer = target;
  ++generation;
  if (dataCurrent) dataFrameReady = true;
  portEXIT_CRITICAL(&stateMux);
}

// --- Zkouška adresy ---------------------------------------------------------
void runProbe() {
  ClockSatellitesConfig config;
  float latitude = 0.0f;
  float longitude = 0.0f;
  portENTER_CRITICAL(&stateMux);
  config = probeConfig;
  latitude = probeLatitude;
  longitude = probeLongitude;
  probeState = ProbeState::Running;
  portEXIT_CRITICAL(&stateMux);

  // Zásobník úlohy leží v PSRAM, takže výsledek může na něm.
  SatelliteProbeResult result;
  double epoch = 0.0;
  if (WiFi.status() != WL_CONNECTED || !currentEpoch(epoch)) {
    strlcpy(result.error, "Hodiny nemají síť nebo přesný čas.", sizeof(result.error));
  } else if (!ensureStorage()) {
    strlcpy(result.error, "Pro družice není dostatek PSRAM.", sizeof(result.error));
  } else {
    SatelliteFeedInfo info;
    SatelliteParseStatus parseStatus = SatelliteParseStatus::Invalid;
    bool urlTooLong = false;
    // Zkouška rozebírá do pracovního seznamu; živá data zůstanou.
    const long length = fetchInto(config, latitude, longitude, result.httpStatus,
                                  info, parseStatus, urlTooLong);
    if (urlTooLong) {
      strlcpy(result.error, "Adresa serveru družic je příliš dlouhá.",
              sizeof(result.error));
    } else if (length == -2) {
      strlcpy(result.error, "Síť je zaneprázdněná, zkus to znovu.",
              sizeof(result.error));
    } else if (length < 0) {
      snprintf(result.error, sizeof(result.error), "%s.",
               downloadFailureText(result.httpStatus, length));
    } else if (parseStatus != SatelliteParseStatus::Ok) {
      strlcpy(result.error,
              parseStatus == SatelliteParseStatus::NotJson
                  ? "Na adrese není server družic."
                  : "Server družic poslal jiný formát.",
              sizeof(result.error));
    } else {
      result.ok = true;
      result.serverTotal = info.total;
      result.elementAgeHours = info.ageHours;
      result.pending = info.pending;
      result.pass = info.pass;
      result.observerDark =
          info.hasSunElevation && info.sunElevationDeg <= DARK_SUN_ELEVATION;
      strlcpy(result.problem, info.problem, sizeof(result.problem));
      // Nejvýš položené družice nad nastavenou výškou, seřazené vkládáním.
      for (size_t index = 0; index < info.count; ++index) {
        const SatelliteTrack &track = scratchTracks[index];
        SatelliteSkyPoint point;
        if (!satelliteTrackAt(track, info, epoch, point) ||
            point.elevationDeg < config.minElevationDeg)
          continue;
        ++result.count;
        SatelliteProbeEntry entry;
        strlcpy(entry.name, track.name, sizeof(entry.name));
        entry.group = track.group;
        entry.azimuthDeg = point.azimuthDeg;
        entry.elevationDeg = point.elevationDeg;
        entry.sunlit = track.sunlit;
        size_t position = result.entryCount;
        while (position > 0 &&
               result.entries[position - 1].elevationDeg < entry.elevationDeg)
          --position;
        if (position >= SATELLITE_PROBE_ENTRIES) continue;
        const size_t last = result.entryCount < SATELLITE_PROBE_ENTRIES
                                ? result.entryCount
                                : SATELLITE_PROBE_ENTRIES - 1;
        for (size_t move = last; move > position; --move)
          result.entries[move] = result.entries[move - 1];
        result.entries[position] = entry;
        if (result.entryCount < SATELLITE_PROBE_ENTRIES) ++result.entryCount;
      }
    }
  }
  portENTER_CRITICAL(&stateMux);
  if (probeAbandoned) {
    probeState = ProbeState::Idle;
    probeAbandoned = false;
  } else {
    *probeResult = result;
    probeState = ProbeState::Done;
  }
  portEXIT_CRITICAL(&stateMux);
}

// --- Úloha ------------------------------------------------------------------
void satelliteTask(void *) {
  ClockSatellitesConfig config;
  float latitude = 0.0f;
  float longitude = 0.0f;
  bool night = false;
  bool english = false;
  bool wantVisible = false;
  bool wantActive = false;
  bool lastNight = false;
  uint32_t lastRevision = 0xffffffff;
  unsigned long lastRenderAt = 0;
  unsigned long lastForcedFetchAt = 0;
  bool renderedSinceVisible = false;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wantVisible ? 200 : 1000));

    portENTER_CRITICAL(&stateMux);
    const bool probePending = probeState == ProbeState::Pending;
    portEXIT_CRITICAL(&stateMux);
    if (probePending) runProbe();

    portENTER_CRITICAL(&stateMux);
    config = requestConfig;
    latitude = requestLatitude;
    longitude = requestLongitude;
    night = redNightMode;
    english = englishLabels;
    const bool wasVisible = wantVisible;
    wantVisible = visible;
    wantActive = active;
    const uint32_t revision = requestRevision;
    bool redraw = redrawRequested;
    redrawRequested = false;
    portEXIT_CRITICAL(&stateMux);
    if (!wantActive) continue;
    if (wantVisible && !wasVisible) renderedSinceVisible = false;

    if (!ensureStorage()) {
      setStatusMessage("Pro družice není dostatek PSRAM");
      continue;
    }

    double epoch = 0.0;
    const bool haveEpoch = currentEpoch(epoch);
    const bool settingsChanged = revision != lastRevision;
    if (settingsChanged) {
      lastRevision = revision;
      redraw = true;
    }
    if (night != lastNight) {
      lastNight = night;
      redraw = true;
    }

    if (WiFi.status() != WL_CONNECTED || !haveEpoch) {
      setStatusMessage("Čekám na síť");
      portENTER_CRITICAL(&stateMux);
      nextFetchAt = millis() + RETRY_INTERVAL_MS;
      portEXIT_CRITICAL(&stateMux);
    } else {
      portENTER_CRITICAL(&stateMux);
      bool fetchNow = fetchNowRequested;
      fetchNowRequested = false;
      const unsigned long dueAt = nextFetchAt;
      const bool expiring =
          haveTracks && epoch > satelliteFeedEndEpoch(liveInfo) - 30.0;
      if (fetchNow) loading = true;
      portEXIT_CRITICAL(&stateMux);
      if (settingsChanged) fetchNow = true;
      // Dráhy docházejí dřív, než by přišel plánovaný dotaz (třeba po
      // zdřeném stažení): stáhne se mimo plán, ale nejvýš jednou za
      // RETRY_INTERVAL_MS, jinak by nedostupný server znamenal dotaz
      // v každé smyčce.
      if (wantVisible && expiring &&
          millis() - lastForcedFetchAt >= RETRY_INTERVAL_MS) {
        fetchNow = true;
        lastForcedFetchAt = millis();
      }
      if (fetchNow || static_cast<long>(millis() - dueAt) >= 0) {
        portENTER_CRITICAL(&stateMux);
        loading = true;
        portEXIT_CRITICAL(&stateMux);
        uint32_t retryMs = 0;
        const bool ok = fetchTracks(config, latitude, longitude, retryMs);
        uint32_t period =
            wantVisible ? static_cast<uint32_t>(config.refreshSeconds) * 1000UL
                        : HIDDEN_PERIOD_MS;
        if (!ok) {
          // Po chybě se čeká dvojnásobek, ať se server nemlátí v normálním
          // tempu; 503 znamená "za chvíli", tak se zkusí dřív.
          period = retryMs != 0 ? retryMs : period * 2;
        } else if (retryMs != 0 && retryMs < period) {
          period = retryMs;
        }
        portENTER_CRITICAL(&stateMux);
        loading = false;
        nextFetchAt = millis() + period;
        portEXIT_CRITICAL(&stateMux);
        redraw = true;
      }
    }

    if (!wantVisible) continue;
    const unsigned long now = millis();
    if (redraw || !renderedSinceVisible || now - lastRenderAt >= RENDER_PERIOD_MS) {
      renderFrame(config, night, english, epoch, haveEpoch);
      lastRenderAt = now;
      renderedSinceVisible = true;
    }
  }
}

}  // namespace

void satelliteServiceBegin() {
  if (taskHandle != nullptr) return;
  if (skyPoints == nullptr) {
    skyPoints = static_cast<SkyPoint *>(heap_caps_calloc(
        SATELLITE_MAX_TRACKS, sizeof(SkyPoint), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (probeResult == nullptr) {
    probeResult = static_cast<SatelliteProbeResult *>(heap_caps_calloc(
        1, sizeof(SatelliteProbeResult), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (probeResult != nullptr) new (probeResult) SatelliteProbeResult();
  }
  // Bez těchto dvou polí by klepnutí i zkouška sahaly do prázdna; obrazovka
  // pak zůstane vypnutá, hodiny poběží dál.
  if (skyPoints == nullptr || probeResult == nullptr) return;
  xTaskCreatePinnedToCoreWithCaps(satelliteTask, "satellites", 20480, nullptr,
                                  1, &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void satelliteServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  active = false;
  visible = false;
  ++requestRevision;
  portEXIT_CRITICAL(&stateMux);
  // OTA drží síťový koordinátor, takže úloha teď nemůže být uvnitř HTTP ani
  // TLS operace. Ukončením úlohy se zastaví i kreslení, než se uvolní buffery.
  if (taskHandle != nullptr) {
    vTaskDeleteWithCaps(taskHandle);
    taskHandle = nullptr;
  }
  portENTER_CRITICAL(&stateMux);
  displayedBuffer = -1;
  handedOutBuffer = -1;
  haveTracks = false;
  dataFrameReady = false;
  loading = false;
  liveCount = 0;
  skyPointCount = 0;
  // Web, který čeká na zkoušku, dostane odpověď místo visení do timeoutu.
  if (probeState == ProbeState::Pending || probeState == ProbeState::Running) {
    *probeResult = SatelliteProbeResult{};
    strlcpy(probeResult->error, "Probíhá aktualizace firmware.",
            sizeof(probeResult->error));
    probeState = probeAbandoned ? ProbeState::Idle : ProbeState::Done;
    probeAbandoned = false;
  }
  ++generation;
  portEXIT_CRITICAL(&stateMux);
  releaseStorage();
}

void satelliteServiceSetActive(bool nowVisible, bool backgroundRefresh,
                               float latitude, float longitude,
                               const ClockSatellitesConfig &config,
                               bool english) {
  bool notify = false;
  // Čas mimo zámek: gettimeofday si bere vlastní zámek a v kritické sekci se
  // čekat nesmí.
  double epoch = 0.0;
  const bool haveEpoch = currentEpoch(epoch);
  portENTER_CRITICAL(&stateMux);
  const bool requestChanged =
      strcmp(requestConfig.url, config.url) != 0 ||
      requestConfig.groups != config.groups ||
      requestConfig.minElevationDeg != config.minElevationDeg ||
      requestLatitude != latitude || requestLongitude != longitude;
  const bool drawingChanged =
      requestConfig.showTracks != config.showTracks ||
      requestConfig.topBearingDeg != config.topBearingDeg ||
      englishLabels != english;
  const bool refreshChanged =
      requestConfig.refreshSeconds != config.refreshSeconds;
  if (requestChanged) {
    // Dráhy z jiné adresy, polohy nebo skupin se nesmí ukázat ani na chvíli.
    haveTracks = false;
    liveCount = 0;
    dataFrameReady = false;
    selectedId = 0;
  }
  requestConfig = config;
  requestLatitude = latitude;
  requestLongitude = longitude;
  englishLabels = english;
  const bool wasActive = active;
  const bool wasVisible = visible;
  visible = nowVisible;
  active = nowVisible || backgroundRefresh;
  if (requestChanged || drawingChanged || refreshChanged) {
    ++requestRevision;
    notify = true;
  }
  if (requestChanged || (active && !wasActive)) {
    fetchNowRequested = true;
    notify = true;
  }
  if (nowVisible && !wasVisible) {
    // Obrazovka se otvírá: dráhy ze schovaného stahování můžou být na hraně
    // okna, takže se stahuje hned, když zbývá málo.
    if (!haveTracks || !haveEpoch ||
        epoch > satelliteFeedEndEpoch(liveInfo) - 60.0)
      fetchNowRequested = true;
    redrawRequested = true;
    notify = true;
  }
  if (!nowVisible) selectedId = 0;
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void satelliteServiceSetRedNightMode(bool enabled) {
  portENTER_CRITICAL(&stateMux);
  const bool changed = redNightMode != enabled;
  redNightMode = enabled;
  if (changed) redrawRequested = true;
  const bool notify = changed && visible;
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void satelliteServiceSnapshot(SatelliteSnapshot &snapshot) {
  double epoch = 0.0;
  const bool haveEpoch = currentEpoch(epoch);
  portENTER_CRITICAL(&stateMux);
  snapshot.pixels =
      displayedBuffer >= 0 ? displayBuffers[displayedBuffer] : nullptr;
  handedOutBuffer = displayedBuffer;
  snapshot.generation = generation;
  snapshot.loading = loading || fetchNowRequested;
  snapshot.haveData = dataFrameReady && haveTracks && haveEpoch &&
                      feedCoversEpoch(liveInfo, epoch);
  snapshot.shownCount = shownCount;
  snapshot.visibleCount = visibleCount;
  snapshot.observerDark = observerDark;
  snapshot.passWanted =
      (requestConfig.groups & CLOCK_SATELLITE_GROUP_STATIONS) != 0;
  snapshot.pass = haveTracks ? liveInfo.pass : SatellitePass{};
  strlcpy(snapshot.message, statusMessage, sizeof(snapshot.message));
  snapshot.detail = SatelliteDetail{};
  if (selectedId != 0 && selectionDetail.noradId == selectedId) {
    snapshot.detail = selectionDetail;
    snapshot.detail.open = true;
  }
  portEXIT_CRITICAL(&stateMux);
}

void satelliteServiceDiagnostics(SatelliteDiagnostics &diagnostics) {
  double epoch = 0.0;
  const bool haveEpoch = currentEpoch(epoch);
  portENTER_CRITICAL(&stateMux);
  diagnostics.available = requestConfig.enabled;
  diagnostics.active = active;
  diagnostics.visible = visible;
  diagnostics.loading = loading;
  diagnostics.haveData =
      haveTracks && haveEpoch && feedCoversEpoch(liveInfo, epoch);
  diagnostics.trackCount = static_cast<uint16_t>(liveCount);
  diagnostics.serverTotal = liveInfo.total;
  diagnostics.elementAgeHours = liveInfo.ageHours;
  const unsigned long now = millis();
  diagnostics.lastSuccessfulRefreshAgeMs =
      lastSuccessAt == 0 ? 0 : static_cast<uint32_t>(now - lastSuccessAt);
  diagnostics.nextRefreshInMs =
      (!fetchNowRequested && static_cast<long>(nextFetchAt - now) > 0)
          ? static_cast<uint32_t>(nextFetchAt - now)
          : 0;
  diagnostics.lastHttpStatus = lastHttpStatus;
  diagnostics.lastDownloadedBytes = lastDownloadedBytes;
  strlcpy(diagnostics.message, statusMessage, sizeof(diagnostics.message));
  strlcpy(diagnostics.serverProblem, liveInfo.problem,
          sizeof(diagnostics.serverProblem));
  portEXIT_CRITICAL(&stateMux);
}

bool satelliteServiceHasCurrentData() {
  double epoch = 0.0;
  const bool haveEpoch = currentEpoch(epoch);
  portENTER_CRITICAL(&stateMux);
  const bool current =
      haveTracks && haveEpoch && feedCoversEpoch(liveInfo, epoch);
  portEXIT_CRITICAL(&stateMux);
  return current;
}

bool satelliteServiceHandleTap(int16_t x, int16_t y) {
  if (skyPoints == nullptr) return false;
  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  if (selectedId != 0) {
    selectedId = 0;
    selectionMissCount = 0;
    changed = true;
  } else {
    int best = -1;
    long bestDistance = 30L * 30L;
    for (uint8_t index = 0; index < skyPointCount; ++index) {
      const long deltaX = skyPoints[index].x - x;
      const long deltaY = skyPoints[index].y - y;
      const long distance = deltaX * deltaX + deltaY * deltaY;
      if (distance < bestDistance) {
        bestDistance = distance;
        best = index;
      }
    }
    if (best >= 0 && skyPoints[best].noradId != 0) {
      selectedId = skyPoints[best].noradId;
      selectionMissCount = 0;
      // Detail naplní až další kreslení; do té doby se neukazuje nic cizího.
      selectionDetail = SatelliteDetail{};
      changed = true;
    }
  }
  if (changed) redrawRequested = true;
  portEXIT_CRITICAL(&stateMux);
  if (changed && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
  return changed;
}

bool satelliteServiceDetailOpen() {
  portENTER_CRITICAL(&stateMux);
  const bool open = selectedId != 0;
  portEXIT_CRITICAL(&stateMux);
  return open;
}

void satelliteServiceCloseDetail() {
  bool notify = false;
  portENTER_CRITICAL(&stateMux);
  if (selectedId != 0) {
    selectedId = 0;
    selectionMissCount = 0;
    redrawRequested = true;
    notify = visible;
  }
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

bool satelliteServiceStartProbe(const ClockSatellitesConfig &config,
                                float latitude, float longitude) {
  if (taskHandle == nullptr) return false;
  portENTER_CRITICAL(&stateMux);
  const bool busy =
      probeState == ProbeState::Pending || probeState == ProbeState::Running;
  if (!busy) {
    probeConfig = config;
    probeLatitude = latitude;
    probeLongitude = longitude;
    probeAbandoned = false;
    probeState = ProbeState::Pending;
  }
  portEXIT_CRITICAL(&stateMux);
  if (busy) return false;
  xTaskNotifyGive(taskHandle);
  return true;
}

bool satelliteServiceProbeResult(SatelliteProbeResult &result) {
  portENTER_CRITICAL(&stateMux);
  const bool done = probeState == ProbeState::Done;
  if (done) {
    result = *probeResult;
    probeState = ProbeState::Idle;
  }
  portEXIT_CRITICAL(&stateMux);
  return done;
}

void satelliteServiceAbandonProbe() {
  portENTER_CRITICAL(&stateMux);
  if (probeState == ProbeState::Done) {
    probeState = ProbeState::Idle;
  } else if (probeState == ProbeState::Pending) {
    probeState = ProbeState::Idle;
  } else if (probeState == ProbeState::Running) {
    probeAbandoned = true;
  }
  portEXIT_CRITICAL(&stateMux);
}
