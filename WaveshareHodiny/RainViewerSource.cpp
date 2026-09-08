#include "RainViewerSource.h"

#include <HTTPClient.h>
#include <PNGdec.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstring>

#include "ChmiRadarService.h"
#include "RainViewerIndex.h"
#include "FirmwareHubCa.h"
#include "NetworkCoordinator.h"

namespace {
constexpr char INDEX_URL[] = "https://api.rainviewer.com/public/weather-maps.json";
constexpr char DEFAULT_HOST[] = "https://tilecache.rainviewer.com";
constexpr int TILE_SIZE = 256;
// Barevná škála 2 = Universal Blue, ke které patří i stupnice na displeji.
// Vyhlazení a sníh zapnuté, stejně jako je servíruje web RainVieweru.
constexpr int TILE_COLOR_SCHEME = 2;
constexpr int TILE_SMOOTH = 1;
constexpr int TILE_SNOW = 1;
// Výš než na sedmičku RainViewer nejde; bližší pohledy se dělají zvětšením
// jednoho pixelu dlaždice na čtverec. Hrubší než ČHMÚ, ale víc dat prostě
// není.
constexpr int MAX_ZOOM = 7;
constexpr int MAX_SCALE = 8;
constexpr int MIN_ZOOM = 3;
constexpr size_t MAX_TILE_PNG_BYTES = 65536;
// Index má pod kilobajt a jeho tvar je pevně daný, takže si vystačíme s
// hledáním klíčů jako zbytek firmwaru; kvůli třem polím nemá smysl přibírat
// celý parser JSON.
constexpr size_t MAX_INDEX_BYTES = 4096;
constexpr int TILE_RETRY_COUNT = 3;
constexpr unsigned long TILE_RETRY_PAUSE_MS = 400;
// Celá ČR: pevný střed a poloměr, stejné hodnoty jako u kompozice ČHMÚ.
constexpr float WHOLE_COUNTRY_LATITUDE = 49.805f;
constexpr float WHOLE_COUNTRY_LONGITUDE = 15.475f;
constexpr float WHOLE_COUNTRY_RADIUS_KM = 260.0f;

RainViewerPollCallback pollCallback = nullptr;
RainViewerRevisionCheck revisionCheck = nullptr;

float viewLatitude = 0.0f;
float viewLongitude = 0.0f;
uint16_t viewRadiusKm = 0xffff;
int zoom = MAX_ZOOM;
int scale = 1;
double originX = 0.0;
double originY = 0.0;
float effectiveRadiusKm = 0.0f;
int tileX0 = 0;
int tileY0 = 0;
int tileColumns = 1;
int tileRows = 1;

char host[64] = "";
// Relace TLS drží celý snímek. Handshake stojí přes sekundu a kus vnitřní
// paměti; devět dlaždic po samostatném spojení by animaci protáhlo na minuty.
WiFiClientSecure tileClient;
uint8_t *tilePng = nullptr;
uint16_t *tileLine = nullptr;
PNG *decoder = nullptr;

// Kam právě dekódovaná dlaždice patří na displeji.
int destinationX = 0;
int destinationY = 0;
uint16_t *destinationFrame = nullptr;

void pump() {
  if (pollCallback != nullptr) pollCallback();
}

bool requestValid(uint32_t revision) {
  return revisionCheck == nullptr || revisionCheck(revision);
}

double worldSize(int level) {
  return 256.0 * static_cast<double>(1UL << level);
}

void longitudeLatitudeToWorld(double latitude, double longitude, int level,
                              double &worldX, double &worldY) {
  const double size = worldSize(level);
  double radians = latitude * 0.01745329252;
  // U pólů roste y nade všechny meze, proto se zeměpisná šířka ořízne.
  if (radians > 1.4835) radians = 1.4835;
  if (radians < -1.4835) radians = -1.4835;
  worldX = (longitude + 180.0) / 360.0 * size;
  worldY = (1.0 - log(tan(0.78539816339 + radians * 0.5)) / M_PI) * 0.5 * size;
}

void worldToLongitudeLatitude(double worldX, double worldY, int level,
                              double &latitude, double &longitude) {
  const double size = worldSize(level);
  longitude = worldX / size * 360.0 - 180.0;
  const double n = M_PI * (1.0 - 2.0 * worldY / size);
  latitude = atan(sinh(n)) * 57.2957795131;
}

double visibleWorldWidth() {
  return static_cast<double>(CHMI_RADAR_WIDTH) / scale;
}

double visibleWorldHeight() {
  return static_cast<double>(CHMI_RADAR_HEIGHT) / scale;
}

// Vybere přiblížení a zvětšení. Vždy nejvyšší dostupné přiblížení: čtyřka
// zvětšená osmkrát pokryje totéž co sedmička s osminou detailu, takže
// zvětšení je až zbytek po stropu, ne volba. Při celé ČR se navíc couvá tak
// dlouho, dokud pohled nepokryje celý požadovaný poloměr - ukázat míň by tam
// bylo horší než ukázat víc.
void chooseZoomAndScale(double latitude, float radiusKm, bool mustCover) {
  const double wanted =
      static_cast<double>(radiusKm) * 1000.0 / (CHMI_RADAR_HEIGHT / 2);
  const double base = 156543.03392 * cos(latitude * 0.01745329252);
  const int ideal = static_cast<int>(lround(log(base / wanted) / log(2.0)));

  int level = ideal;
  if (level > MAX_ZOOM) level = MAX_ZOOM;
  if (level < MIN_ZOOM) level = MIN_ZOOM;

  int factor = 1;
  if (ideal > level) {
    const int shift = ideal - level;
    factor = shift >= 4 ? MAX_SCALE : (1 << shift);
    if (factor > MAX_SCALE) factor = MAX_SCALE;
  }

  if (mustCover) {
    while ((base / static_cast<double>(1UL << level) / factor) < wanted &&
           (factor > 1 || level > MIN_ZOOM)) {
      if (factor > 1)
        factor >>= 1;
      else
        --level;
    }
  }

  zoom = level;
  scale = factor;
  const double metersPerPixel =
      base / static_cast<double>(1UL << zoom) / scale;
  effectiveRadiusKm =
      static_cast<float>(metersPerPixel * (CHMI_RADAR_HEIGHT / 2) / 1000.0);
}

void computeGrid() {
  float latitude = viewLatitude;
  float longitude = viewLongitude;
  float radiusKm = static_cast<float>(viewRadiusKm);
  const bool wholeCountry = viewRadiusKm == 0;
  if (wholeCountry) {
    latitude = WHOLE_COUNTRY_LATITUDE;
    longitude = WHOLE_COUNTRY_LONGITUDE;
    radiusKm = WHOLE_COUNTRY_RADIUS_KM;
  }

  chooseZoomAndScale(latitude, radiusKm, wholeCountry);
  double worldX = 0.0;
  double worldY = 0.0;
  longitudeLatitudeToWorld(latitude, longitude, zoom, worldX, worldY);
  originX = worldX - visibleWorldWidth() / 2.0;
  originY = worldY - visibleWorldHeight() / 2.0;

  tileX0 = static_cast<int>(floor(originX / TILE_SIZE));
  tileY0 = static_cast<int>(floor(originY / TILE_SIZE));
  const int lastX =
      static_cast<int>(floor((originX + visibleWorldWidth() - 1) / TILE_SIZE));
  const int lastY =
      static_cast<int>(floor((originY + visibleWorldHeight() - 1) / TILE_SIZE));
  tileColumns = lastX - tileX0 + 1;
  tileRows = lastY - tileY0 + 1;
  if (tileColumns < 1) tileColumns = 1;
  if (tileRows < 1) tileRows = 1;
}

bool ensureBuffers() {
  if (tilePng == nullptr) {
    tilePng = static_cast<uint8_t *>(
        heap_caps_malloc(MAX_TILE_PNG_BYTES, MALLOC_CAP_SPIRAM));
    if (tilePng == nullptr)
      tilePng = static_cast<uint8_t *>(malloc(MAX_TILE_PNG_BYTES));
    if (tilePng == nullptr) return false;
  }
  if (tileLine == nullptr) {
    // Jeden dekódovaný řádek dlaždice. Vnitřní RAM: PNGdec do něj zapisuje
    // pixel po pixelu a v PSRAM by se to vleklo.
    tileLine = static_cast<uint16_t *>(malloc(TILE_SIZE * sizeof(uint16_t)));
    if (tileLine == nullptr) return false;
  }
  if (decoder == nullptr) {
    decoder = new (std::nothrow) PNG();
    if (decoder == nullptr) return false;
  }
  return true;
}

// Jeden dekódovaný řádek dlaždice, roztažený měřítkem do snímku. Při měřítku
// 1 jde o prosté kopírování, výš se z každého zdrojového pixelu stane čtverec.
// Záměrně nejbližší soused: dlaždice jsou už vyhlazené na serveru a
// interpolace odrazivosti by vymýšlela intenzity, které v datech nejsou.
void drawTileLine(PNGDRAW *draw) {
  if (destinationFrame == nullptr || tileLine == nullptr) return;
  // Řádkový buffer má velikost dlaždice, o kterou jsme si řekli. Kdyby server
  // někdy poslal širší, dekódování by přeteklo alokaci.
  if (draw->iWidth > TILE_SIZE) return;

  const int step = scale;
  const int topY = destinationY + draw->y * step;
  if (topY + step <= 0 || topY >= CHMI_RADAR_HEIGHT) return;

  // Průhledné pixely (mimo dosah radaru) splynou s černou, tedy s tím, co je
  // na zbytku obrazovky - "bez dat" a "neprší" vypadá stejně jako u ČHMÚ.
  decoder->getLineAsRGB565(draw, tileLine, PNG_RGB565_LITTLE_ENDIAN,
                           0x00000000);

  int firstSource = 0;
  // Kolik pixelů řádku server opravdu poslal. Užší dlaždici getLineAsRGB565
  // naplní jen po svoji šířku a za ní by v řádkovém bufferu zůstaly pixely
  // z dlaždice předchozí, které by se rozmazaly do mapy.
  int lastSource = draw->iWidth - 1;
  if (destinationX < 0) firstSource = (-destinationX + step - 1) / step;
  const int maxSource = (CHMI_RADAR_WIDTH - 1 - destinationX) / step;
  if (maxSource < lastSource) lastSource = maxSource;
  if (firstSource > lastSource) return;

  if (step == 1) {
    if (topY < 0 || topY >= CHMI_RADAR_HEIGHT) return;
    memcpy(destinationFrame + static_cast<int32_t>(topY) * CHMI_RADAR_WIDTH +
               (destinationX + firstSource),
           tileLine + firstSource,
           static_cast<size_t>(lastSource - firstSource + 1) *
               sizeof(uint16_t));
    return;
  }

  for (int row = 0; row < step; ++row) {
    const int y = topY + row;
    if (y < 0 || y >= CHMI_RADAR_HEIGHT) continue;
    uint16_t *target =
        destinationFrame + static_cast<int32_t>(y) * CHMI_RADAR_WIDTH;
    for (int source = firstSource; source <= lastSource; ++source) {
      const uint16_t color = tileLine[source];
      int x = destinationX + source * step;
      for (int repeat = 0; repeat < step; ++repeat, ++x) {
        if (x < 0 || x >= CHMI_RADAR_WIDTH) continue;
        target[x] = color;
      }
    }
  }
}

bool downloadTile(HTTPClient &http, const RainViewerFrame &frame,
                  int tileIndex, uint16_t *target, uint32_t revision) {
  const int column = tileIndex % tileColumns;
  const int row = tileIndex / tileColumns;
  const int tileX = tileX0 + column;
  const int tileY = tileY0 + row;

  const int worldTiles = 1 << zoom;
  int wrappedX = tileX % worldTiles;
  if (wrappedX < 0) wrappedX += worldTiles;
  // Nad pólem ani pod ním žádná dlaždice není; prázdné místo zůstane černé.
  if (tileY < 0 || tileY >= worldTiles) return true;

  char url[224];
  snprintf(url, sizeof(url), "%s%s/%d/%d/%d/%d/%d/%d_%d.png", host, frame.path,
           TILE_SIZE, zoom, wrappedX, tileY, TILE_COLOR_SCHEME, TILE_SMOOTH,
           TILE_SNOW);

  if (!http.begin(tileClient, url)) return false;
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    // Tělo chybové odpovědi zůstalo nepřečtené a keep-alive spojení stojí na
    // tom, že se odpověď dočte celá; další dlaždice by si ho jinak přečetla
    // jako svoji. Zahodit spojení je stejné jako v ostatních chybových cestách.
    tileClient.stop();
    return false;
  }

  const int declaredSize = http.getSize();
  if (declaredSize < 0 || declaredSize > static_cast<int>(MAX_TILE_PNG_BYTES)) {
    http.end();
    tileClient.stop();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t received = 0;
  int remaining = declaredSize;
  unsigned long lastDataAt = millis();
  while (remaining > 0) {
    if (!requestValid(revision)) {
      http.end();
      tileClient.stop();
      return false;
    }
    const size_t available = stream->available();
    if (available == 0) {
      const unsigned long idleFor = millis() - lastDataAt;
      if (idleFor > 15000 || (!http.connected() && idleFor > 750)) break;
      pump();
      delay(2);
      continue;
    }
    const size_t wanted = min(available, MAX_TILE_PNG_BYTES - received);
    if (wanted == 0) break;
    const int bytesRead = stream->readBytes(tilePng + received, wanted);
    if (bytesRead <= 0) break;
    received += static_cast<size_t>(bytesRead);
    lastDataAt = millis();
    remaining -= bytesRead;
    pump();
  }
  // Keep-alive stojí na tom, že se tělo odpovědi přečte celé. Nedočtený zbytek
  // by zůstal v bufferu a další požadavek by si ho přečetl jako svoji odpověď,
  // proto se takové spojení zahazuje.
  const bool bodyComplete = received == static_cast<size_t>(declaredSize);
  http.end();
  if (!bodyComplete) {
    tileClient.stop();
    return false;
  }

  static const uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r',
                                      '\n', 0x1a, '\n'};
  if (received < sizeof(signature) ||
      memcmp(tilePng, signature, sizeof(signature)) != 0) {
    return false;
  }

  destinationX = static_cast<int>(lround(
      (tileX * static_cast<double>(TILE_SIZE) - originX) * scale));
  destinationY = static_cast<int>(lround(
      (tileY * static_cast<double>(TILE_SIZE) - originY) * scale));
  destinationFrame = target;
  if (decoder->openRAM(tilePng, static_cast<int>(received), drawTileLine) !=
      PNG_SUCCESS) {
    destinationFrame = nullptr;
    return false;
  }
  decoder->decode(nullptr, 0);
  decoder->close();
  destinationFrame = nullptr;
  pump();
  return true;
}
}  // namespace

bool rainViewerSetView(float latitude, float longitude, uint16_t radiusKm) {
  if (fabsf(viewLatitude - latitude) < 0.000001f &&
      fabsf(viewLongitude - longitude) < 0.000001f &&
      viewRadiusKm == radiusKm) {
    return false;
  }
  viewLatitude = latitude;
  viewLongitude = longitude;
  viewRadiusKm = radiusKm;
  computeGrid();
  return true;
}

uint16_t rainViewerEffectiveRadiusKm() {
  return static_cast<uint16_t>(lroundf(effectiveRadiusKm));
}

void rainViewerProject(float latitude, float longitude, int &x, int &y) {
  double worldX = 0.0;
  double worldY = 0.0;
  longitudeLatitudeToWorld(latitude, longitude, zoom, worldX, worldY);
  x = static_cast<int>(lround((worldX - originX) * scale));
  y = static_cast<int>(lround((worldY - originY) * scale));
}

void rainViewerWindow(float &southLatitude, float &northLatitude,
                      float &westLongitude, float &eastLongitude) {
  double topLatitude = 0.0;
  double topLongitude = 0.0;
  double bottomLatitude = 0.0;
  double bottomLongitude = 0.0;
  worldToLongitudeLatitude(originX, originY, zoom, topLatitude, topLongitude);
  worldToLongitudeLatitude(originX + visibleWorldWidth(),
                           originY + visibleWorldHeight(), zoom,
                           bottomLatitude, bottomLongitude);
  southLatitude = static_cast<float>(bottomLatitude);
  northLatitude = static_cast<float>(topLatitude);
  westLongitude = static_cast<float>(topLongitude);
  eastLongitude = static_cast<float>(bottomLongitude);
}

bool rainViewerFetchIndex(size_t wantedFrameCount, RainViewerIndex &index,
                          uint32_t revision) {
  index = RainViewerIndex{};
  if (wantedFrameCount == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  NetworkOperationGuard networkGuard(15000);
  if (!networkGuard) return false;
  WiFiClientSecure client;
  client.setCACert(FIRMWARE_RELEASE_ROOT_CA);
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(6000);
  http.setTimeout(15000);
  if (!http.begin(client, INDEX_URL)) return false;
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  static char payload[MAX_INDEX_BYTES];
  size_t length = 0;
  WiFiClient *stream = http.getStreamPtr();
  int remaining = http.getSize();
  unsigned long lastDataAt = millis();
  while ((remaining > 0 || remaining == -1) && length + 1 < sizeof(payload)) {
    if (!requestValid(revision)) {
      http.end();
      return false;
    }
    const size_t available = stream->available();
    if (available == 0) {
      const unsigned long idleFor = millis() - lastDataAt;
      if (idleFor > 15000 || (!http.connected() && idleFor > 750)) break;
      pump();
      delay(2);
      continue;
    }
    const size_t wanted = min(available, sizeof(payload) - 1 - length);
    const int bytesRead = stream->readBytes(payload + length, wanted);
    if (bytesRead <= 0) break;
    length += static_cast<size_t>(bytesRead);
    lastDataAt = millis();
    if (remaining > 0) remaining -= bytesRead;
    pump();
  }
  http.end();
  payload[length] = '\0';
  if (length == 0) return false;
  if (!rainViewerParseIndex(payload, wantedFrameCount, index)) return false;
  strlcpy(host, index.host, sizeof(host));
  return true;
}

bool rainViewerBuildFrame(const RainViewerFrame &frame, uint16_t *target,
                          uint32_t revision) {
  if (target == nullptr || frame.path[0] == '\0') return false;
  if (!ensureBuffers()) return false;
  if (host[0] == '\0') strlcpy(host, DEFAULT_HOST, sizeof(host));

  // Rozestavěný snímek nesmí ukázat pixely po předchozím pohledu.
  memset(target, 0, static_cast<size_t>(CHMI_RADAR_WIDTH) *
                        CHMI_RADAR_HEIGHT * sizeof(uint16_t));

  NetworkOperationGuard networkGuard(15000);
  if (!networkGuard) return false;
  tileClient.setCACert(FIRMWARE_RELEASE_ROOT_CA);
  HTTPClient http;
  // Záměrně bez useHTTP10(): ta v jádře ESP32 vypíná i keep-alive, a devět
  // dlaždic po vlastním handshake je rozdíl mezi vteřinami a minutami.
  // Dlaždice chodí z CDN vždy s Content-Length, takže rámování chunked, kvůli
  // kterému existuje HttpBodyReader, tady nehrozí - a kdyby přece, download se
  // níž rozpozná podle chybějící délky a odmítne.
  http.setReuse(true);
  http.setConnectTimeout(6000);
  http.setTimeout(15000);

  const int tiles = tileColumns * tileRows;
  size_t delivered = 0;
  for (int index = 0; index < tiles; ++index) {
    if (!requestValid(revision)) {
      tileClient.stop();
      return false;
    }
    bool loaded = false;
    for (int attempt = 0; attempt <= TILE_RETRY_COUNT && !loaded; ++attempt) {
      if (attempt > 0) {
        // Neúspěch často znamená, že spojení je pryč. Zahodíme ho, aby si další
        // pokus postavil nové místo zápisu do mrtvého socketu.
        tileClient.stop();
        // Vzdát to po prvním neúspěchu znamená trvale černý čtverec. Přerušené
        // spojení nebo chvilkový nedostatek haldy bývá do dalšího pokusu pryč.
        const unsigned long until = millis() + TILE_RETRY_PAUSE_MS;
        while (static_cast<long>(millis() - until) < 0) {
          pump();
          delay(10);
        }
        if (!requestValid(revision)) {
          tileClient.stop();
          return false;
        }
      }
      loaded = downloadTile(http, frame, index, target, revision);
    }
    if (loaded) ++delivered;
  }
  tileClient.stop();
  return delivered > 0;
}

void rainViewerRelease() {
  tileClient.stop();
  if (decoder != nullptr) {
    delete decoder;
    decoder = nullptr;
  }
  if (tilePng != nullptr) {
    free(tilePng);
    tilePng = nullptr;
  }
  if (tileLine != nullptr) {
    free(tileLine);
    tileLine = nullptr;
  }
  // Další zapnutí zdroje si mřížku spočítá znovu.
  viewRadiusKm = 0xffff;
}

void rainViewerSetPollCallback(RainViewerPollCallback callback) {
  pollCallback = callback;
}

void rainViewerSetRevisionCheck(RainViewerRevisionCheck check) {
  revisionCheck = check;
}
