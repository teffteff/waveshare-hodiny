#include "PlaneRadarService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "CzechMapData.h"
#include "EuropeMapData.h"
#include "HttpDownload.h"
#include "JsonScan.h"
#include "MapCanvas.h"
#include "NetworkCoordinator.h"

// Kořenové certifikáty Mozilly slinkované v mbedTLS. adsb.fi ani adsb.lol
// nejsou naše servery a jejich certifikát se může kdykoli přepnout na jiný
// kořen, takže se připnout nedá - stejně jako u kanálu zpráv.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {

constexpr char ADSB_HOST[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr char ROUTE_HOST[] = "https://api.adsb.lol/api/0/route/";
// adsb.fi i adsb.lol prosí, ať se volající představí; adsb.lol navíc odmítá
// User-Agent bez kontaktu. Jeden řetězec pro obojí, ať je to na jednom místě.
constexpr char USER_AGENT[] =
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)";

constexpr uint32_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t NETWORK_GUARD_MS = 10000;
// Při dosahu 100 km nad hustou Evropou má odpověď desítky až stovky kB. Přes
// strop se zahazuje celá - useknutý JSON se rozebrat nedá - takže by těsný
// limit znamenal, že nejširší dosah nad rušnou oblohou nefunguje vůbec. Buffer
// leží v PSRAM a používá se dokola, takže velkorysost tady nic nestojí.
constexpr size_t MAX_RESPONSE_BYTES = 512 * 1024;
constexpr size_t MAX_ROUTE_RESPONSE_BYTES = 8 * 1024;
constexpr time_t VALID_TIME_THRESHOLD = 1700000000;

// Bez času ze sítě by TLS odmítlo každý certifikát jako "ještě neplatný".
constexpr uint32_t RETRY_INTERVAL_MS = 15000;

constexpr float KM_PER_NAUTICAL_MILE = 1.852f;
constexpr float DEGREES_TO_RADIANS = 0.0174532925f;
constexpr float KM_PER_DEGREE_LATITUDE = 111.0f;

constexpr int RADAR_CENTER_X = PLANE_RADAR_WIDTH / 2;
constexpr int RADAR_CENTER_Y = PLANE_RADAR_HEIGHT / 2;
// Poloměr kruhu, na který se promítá nastavený dosah.
constexpr int RADAR_RADIUS = 230;

constexpr size_t PIXEL_COUNT =
    static_cast<size_t>(PLANE_RADAR_WIDTH) * PLANE_RADAR_HEIGHT;

// Kolik po sobě jdoucích stahování smí letadlo v datech chybět, než se detail
// zavře. adsb.fi občas jedno vynechá a v dalším ho pošle zas; zavírat panel
// hned by vypadalo, že se zavírá sám.
constexpr uint8_t DETAIL_GRACE_POLLS = 3;

// Barvy RGB565. Výška letadla je v barvě ikony, takže se pásma nesmí rozejít
// s legendou dole.
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr uint16_t COLOR_WHITE = 0xffff;
constexpr uint16_t COLOR_GRAY = 0x8410;
constexpr uint16_t COLOR_DARK_GRAY = 0x4208;
constexpr uint16_t COLOR_LOW = 0xf800;      // pod 2 km
constexpr uint16_t COLOR_MID = 0xfd20;      // 2-6 km
constexpr uint16_t COLOR_HIGH = 0xffe0;     // 6-10 km
constexpr uint16_t COLOR_CRUISE = 0x3cbb;   // 10 km a výš
constexpr uint16_t COLOR_UNKNOWN = 0x8410;  // výšku nehlásí
constexpr uint16_t COLOR_WATCHED = 0x6628;

// --- Stav -------------------------------------------------------------------
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t taskHandle = nullptr;

// Dva snímkové buffery. Kreslí se vždy do toho, který zrovna není na displeji:
// renderFrame() buffer nejdřív přemaže černou a při kreslení uvolňuje procesor,
// takže by LVGL jinak vykreslil rozdělanou mapu.
constexpr size_t DISPLAY_BUFFER_COUNT = 2;
uint16_t *displayBuffers[DISPLAY_BUFFER_COUNT] = {};
// Index bufferu, který je hotový a smí se zobrazit; -1 dokud nic nakresleno není.
int displayedBuffer = -1;
// Index bufferu, který si naposledy odnesla obrazovka. LVGL si ukazatel drží,
// dokud nedostane jiný, takže právě do tohohle se kreslit nesmí - odvozovat to
// z displayedBuffer by nestačilo, kdyby se stihly dva snímky mezi dvěma
// překresleními displeje.
int handedOutBuffer = -1;
// Buffer, do kterého se právě kreslí. Drží ho jen úloha radaru, takže všechny
// kreslicí funkce můžou psát sem a nemusí si cíl podávat parametrem.
uint16_t *pixels = nullptr;
AdsbAircraft *liveList = nullptr;
AdsbAircraft *scratchList = nullptr;
uint8_t *responseBuffer = nullptr;

// Požadavek z obrazovky.
bool active = false;
bool visible = false;
bool redNightMode = false;
float requestLatitude = 49.1951f;
float requestLongitude = 16.6068f;
ClockPlanesConfig requestPlanes;
// Dosah naposledy přijatý z konfigurace. Přetažení prstem mění jen běžící
// dosah, ne uložený - a bez téhle pamatováky by ho každé předání konfigurace
// (návrat na obrazovku, synchronizace času, uložení čehokoli na webu) srazilo
// zpátky na uloženou hodnotu. Z webu se tedy převezme jen tehdy, když se
// uložený dosah opravdu změnil.
uint8_t configuredRangeIndex = 1;
uint32_t requestRevision = 0;

// Data.
uint8_t aircraftCount = 0;
bool ready = false;
bool loading = false;
uint32_t generation = 0;
unsigned long lastSuccessAt = 0;
unsigned long nextFetchAt = 0;
// "Stáhni hned" se drží příznakem, ne nulou v nextFetchAt. Nula porovnaná přes
// static_cast<long>(millis() - 0) přestane platit, jakmile millis() přeteče přes
// 2^31 - tedy zhruba od 25. do 50. dne běhu - a radar by v tom okně po otevření
// obrazovky nebo změně dosahu přestal stahovat úplně.
bool fetchNowRequested = true;
int lastHttpStatus = 0;
size_t lastDownloadedBytes = 0;
// Prázdná hláška znamená "nic mimořádného". Dokud nedorazí první snímek, píše
// obrazovka svoje "Načítám letadla…" - uvodní text tady by ji přebil a tvářil
// se jako varování.
char statusMessage[64] = "";
char serverMessage[48] = "";

// Výsledek posledního kreslení.
uint8_t shownCount = 0;
bool watchedVisible = false;
char frameEmergency[6] = "";
bool redrawRequested = false;

// Výběr letadla. Drží se ICAO adresa, ne index: pole se staví při každém
// stahování znovu a jeho pořadí není zaručené, takže by index po chvíli
// ukazoval na úplně jiné letadlo.
char selectedHex[8] = "";
uint8_t selectionMissCount = 0;
AdsbAircraft selectionCache;
bool selectionCacheValid = false;

// Poloha letadel z posledního kreslení, pro výběr klepnutím. Drží se i adresa,
// takže klepnutí vybere letadlo, ne pozici v poli.
struct PlanePoint {
  int16_t x = -32768;
  int16_t y = -32768;
  char hex[8] = "";
};
PlanePoint planePoints[ADSB_MAX_AIRCRAFT];
uint8_t planePointCount = 0;

// Trasa vybraného letu. Ptáme se vždy jen na jedno letadlo a odpověď se drží,
// takže přepínání mezi dvěma letadly už API nezatěžuje.
char routeCallsign[10] = "";
RouteInfo routeInfo;
bool routePending = false;
bool routeKnown = false;
int lastRouteHttpStatus = 0;

// Otáčení mapy. Uživatel volí AZIMUT, KTERÝ JE NAHOŘE, tedy směr, kterým se
// dívá z okna:
//     úhel na displeji = azimut - topBearing      (0 = nahoru, po směru)
// Neotáčí se displej (buffer i dotyk by se rozešly), otáčí se projekce.
float rotationSin = 0.0f;
float rotationCos = 1.0f;
uint16_t rotationDegrees = 0;

void setRotation(uint16_t degrees) {
  if (degrees == rotationDegrees) return;
  rotationDegrees = degrees;
  const float radians = static_cast<float>(degrees) * DEGREES_TO_RADIANS;
  rotationSin = sinf(radians);
  rotationCos = cosf(radians);
}

// Noční režim: všechno se převede do odstínů červené, aby obrazovka v ložnici
// nesvítila do očí. Převádí se při výběru barvy, ne přejezdem přes 230 tisíc
// pixelů po vykreslení.
bool nightPalette = false;

uint16_t paletteColor(uint16_t color) {
  if (!nightPalette) return color;
  const uint16_t red = ((color >> 11) & 0x1f) << 3;
  const uint16_t green = ((color >> 5) & 0x3f) << 2;
  const uint16_t blue = (color & 0x1f) << 3;
  const uint16_t luma = (77 * red + 150 * green + 29 * blue) >> 8;
  return static_cast<uint16_t>((luma >> 3) << 11);
}

float rangeKmForIndex(uint8_t index) {
  if (index >= CLOCK_PLANE_RANGE_COUNT) index = 1;
  return static_cast<float>(CLOCK_PLANE_RANGES_KM[index]);
}

// Nejkratší perioda podle dosahu. Větší oblast vrací víc dat a tolik na
// vteřině nezáleží, takže se ptá řidčeji - jen slušnost k API zdarma.
uint32_t minimumPeriodMs(float rangeKm) {
  if (rangeKm <= 25.0f) return 5000;
  if (rangeKm <= 50.0f) return 10000;
  return 15000;
}

// Perioda pro schovanou obrazovku. Zapojení do střídání drží stahování i tehdy,
// když se na radar nikdo nedívá, ale ptát se každých pět vteřin na data, která
// nikdo neuvidí, je vůči API zdarma neslušné - za den by to bylo přes patnáct
// tisíc dotazů kvůli obrazovce, která je vidět zlomek času. Stačí, aby byl
// snímek po ruce, až na obrazovku přijde řada; otevření si stažení vynutí samo.
constexpr uint32_t HIDDEN_PERIOD_MS = 300000;

uint32_t fetchPeriodMs(const ClockPlanesConfig &planes, bool screenVisible) {
  const float rangeKm = rangeKmForIndex(planes.rangeIndex);
  const uint32_t configured = static_cast<uint32_t>(planes.refreshSeconds) * 1000;
  const uint32_t minimum = minimumPeriodMs(rangeKm);
  const uint32_t period = configured > minimum ? configured : minimum;
  if (screenVisible) return period;
  return period > HIDDEN_PERIOD_MS ? period : HIDDEN_PERIOD_MS;
}

void setStatusMessage(const char *text) {
  // Hlášku čte obrazovka z jiné úlohy, takže se nesmí zapsat po půlce.
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
  if (liveList == nullptr) {
    liveList = static_cast<AdsbAircraft *>(
        heap_caps_calloc(ADSB_MAX_AIRCRAFT, sizeof(AdsbAircraft),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (scratchList == nullptr) {
    scratchList = static_cast<AdsbAircraft *>(
        heap_caps_calloc(ADSB_MAX_AIRCRAFT, sizeof(AdsbAircraft),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (responseBuffer == nullptr) {
    responseBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return liveList != nullptr && scratchList != nullptr &&
         responseBuffer != nullptr;
}

void releaseStorage() {
  for (uint16_t *&buffer : displayBuffers) {
    if (buffer == nullptr) continue;
    heap_caps_free(buffer);
    buffer = nullptr;
  }
  displayedBuffer = -1;
  handedOutBuffer = -1;
  pixels = nullptr;
  if (liveList != nullptr) {
    heap_caps_free(liveList);
    liveList = nullptr;
  }
  if (scratchList != nullptr) {
    heap_caps_free(scratchList);
    scratchList = nullptr;
  }
  if (responseBuffer != nullptr) {
    heap_caps_free(responseBuffer);
    responseBuffer = nullptr;
  }
}

// Zapisuje tělo odpovědi do bufferu v PSRAM. Nad strop se přestane přijímat a
// nahlásí se přetečení, aby se nerozebíral useknutý dokument.
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


// --- Projekce ---------------------------------------------------------------
// Poloha do souřadnic displeje, sever nahoře (nebo ten azimut, který si
// uživatel zvolil). Hranice i města jdou touž projekcí, takže se otočí s
// mapou zadarmo; opravit směr navíc musí jen ikony letadel.
void project(float latitude, float longitude, float centerLatitude,
             float centerLongitude, float rangeKm, int &x, int &y) {
  const float latitudeRadians = centerLatitude * DEGREES_TO_RADIANS;
  const float eastKm = (longitude - centerLongitude) * KM_PER_DEGREE_LATITUDE *
                       cosf(latitudeRadians);
  const float northKm = (latitude - centerLatitude) * KM_PER_DEGREE_LATITUDE;
  // Otočení místního východ/sever tak, aby zvolený azimut skončil nahoře.
  const float rotatedX = eastKm * rotationCos - northKm * rotationSin;
  const float rotatedY = eastKm * rotationSin + northKm * rotationCos;
  const float scale = static_cast<float>(RADAR_RADIUS) / rangeKm;
  // Souřadnice přišly z cizí odpovědi. Nesmyslná poloha dá po přepočtu číslo,
  // které se do intu nevejde, a převod takového floatu je nedefinovaný - na
  // výsledek se pak nedá spolehnout ani v kontrole kruhu. Ořízne se proto dřív,
  // než se převádí; hodnota daleko za okrajem displeje stejně akorát vypadne.
  constexpr float LIMIT = 1.0e6f;
  const auto toPixel = [](float value) {
    if (!isfinite(value)) return 0;
    if (value > LIMIT) return static_cast<int>(LIMIT);
    if (value < -LIMIT) return -static_cast<int>(LIMIT);
    return static_cast<int>(value);
  };
  x = RADAR_CENTER_X + toPixel(rotatedX * scale);
  y = RADAR_CENTER_Y - toPixel(rotatedY * scale);
}

struct RenderContext {
  float centerLatitude = 0.0f;
  float centerLongitude = 0.0f;
  float rangeKm = 25.0f;
  // Okno, které může být vidět. Mapová data pokrývají celou Evropu, takže se
  // všechno mimo zahodí dřív, než se začne kreslit.
  float southLatitude = 0.0f;
  float northLatitude = 0.0f;
  float westLongitude = 0.0f;
  float eastLongitude = 0.0f;
};

void projectInContext(const RenderContext &context, float latitude,
                      float longitude, int &x, int &y) {
  project(latitude, longitude, context.centerLatitude, context.centerLongitude,
          context.rangeKm, x, y);
}

// Souřadnice sem chodí z projekce, tedy z čísel, která poslal cizí server.
// Nesmyslná poloha (letadlo na nule stupňů při dosahu 10 km) vyjde na statisíce
// pixelů a součet čtverců by se v int přetekl do záporných čísel - bod daleko za
// obzorem by se tvářil, že je uprostřed kruhu. Počítá se proto v int64.
bool insideCircle(int x, int y, int radius) {
  const int64_t deltaX = static_cast<int64_t>(x) - RADAR_CENTER_X;
  const int64_t deltaY = static_cast<int64_t>(y) - RADAR_CENTER_Y;
  return deltaX * deltaX + deltaY * deltaY <=
         static_cast<int64_t>(radius) * radius;
}

// --- Mapa -------------------------------------------------------------------
void drawEuropeBorders(const RenderContext &context) {
  const auto pointLongitude = [](uint16_t value) {
    return EU_LON_ORIGIN + value * EU_COORD_SCALE;
  };
  const auto pointLatitude = [](uint16_t value) {
    return EU_LAT_ORIGIN + value * EU_COORD_SCALE;
  };
  const uint16_t borderColor = paletteColor(COLOR_GRAY);

  for (int ring = 0; ring < EU_RING_COUNT; ++ring) {
    const uint16_t first = EU_RING_OFFSETS[ring];
    const uint16_t last = EU_RING_OFFSETS[ring + 1];
    if (last - first < 2) continue;

    // Obálka celého prstence: když mine okno, přeskočí se rovnou celý stát.
    // Právě tohle drží kreslení levné - při dosahu 25 km takhle odpadne skoro
    // všechno.
    uint16_t minX = 0xffff;
    uint16_t maxX = 0;
    uint16_t minY = 0xffff;
    uint16_t maxY = 0;
    for (uint16_t index = first; index < last; ++index) {
      const uint16_t x = EU_BORDER_PTS[index][0];
      const uint16_t y = EU_BORDER_PTS[index][1];
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
    if (pointLongitude(maxX) < context.westLongitude ||
        pointLongitude(minX) > context.eastLongitude)
      continue;
    if (pointLatitude(maxY) < context.southLatitude ||
        pointLatitude(minY) > context.northLatitude)
      continue;

    int previousX = 0;
    int previousY = 0;
    bool previousInside = false;
    bool firstPoint = true;
    for (uint16_t index = first; index < last; ++index) {
      const float longitude = pointLongitude(EU_BORDER_PTS[index][0]);
      const float latitude = pointLatitude(EU_BORDER_PTS[index][1]);
      const bool inside = latitude >= context.southLatitude &&
                          latitude <= context.northLatitude &&
                          longitude >= context.westLongitude &&
                          longitude <= context.eastLongitude;
      int x = 0;
      int y = 0;
      projectInContext(context, latitude, longitude, x, y);
      // Úsečka se kreslí, jakmile je v okně kterýkoli z jejích konců. Kdyby se
      // vyžadovaly oba, hranice by u kraje displeje končila utnutá.
      if (!firstPoint && (inside || previousInside))
        drawMapLine(pixels, previousX, previousY, x, y, borderColor, 100);
      previousX = x;
      previousY = y;
      previousInside = inside;
      firstPoint = false;
    }
    // Kreslení celé Evropy je dlouhé; bez uvolnění procesoru by se ozval
    // hlídací pes.
    if ((ring & 15) == 0) vTaskDelay(1);
  }
}

void drawCzechBorder(const RenderContext &context) {
  const uint16_t borderColor = paletteColor(COLOR_GRAY);
  int previousX = 0;
  int previousY = 0;
  for (size_t index = 0;
       index < sizeof(CZECH_MAP_BORDER) / sizeof(CZECH_MAP_BORDER[0]);
       ++index) {
    const float longitude =
        CZECH_MAP_LON_ORIGIN +
        CZECH_MAP_BORDER[index].longitude * CZECH_MAP_COORD_SCALE;
    const float latitude =
        CZECH_MAP_LAT_ORIGIN +
        CZECH_MAP_BORDER[index].latitude * CZECH_MAP_COORD_SCALE;
    int x = 0;
    int y = 0;
    projectInContext(context, latitude, longitude, x, y);
    if (index > 0)
      drawMapLine(pixels, previousX, previousY, x, y, borderColor, 100);
    previousX = x;
    previousY = y;
  }
}

// Jedno město: tečka a vedle ní popisek. Vrací false, když se na displej nebo
// mezi ostatní popisky nevešlo.
bool drawCity(const RenderContext &context, MapLabelPlacer &placer,
              float latitude, float longitude, const char *label) {
  int x = 0;
  int y = 0;
  projectInContext(context, latitude, longitude, x, y);
  if (!insideCircle(x, y, RADAR_RADIUS - 8)) return false;

  const int textWidth = mapTextWidth(label);
  MapLabelBox box = {x + 6, y - 5, textWidth + 4, 11};
  if (box.x + box.width >= PLANE_RADAR_WIDTH) box.x = x - 6 - box.width;
  if (box.x < 0 || box.y < 0 || box.y + box.height >= PLANE_RADAR_HEIGHT)
    return false;
  if (!placer.claim(box)) return false;

  const uint16_t dotColor = paletteColor(COLOR_WHITE);
  const uint16_t labelColor = paletteColor(COLOR_GRAY);
  fillMapCircle(pixels, x, y, 2, dotColor, 100);
  fillMapRect(pixels, box.x, box.y, box.width, box.height,
              paletteColor(COLOR_BLACK), 100);
  drawMapText(pixels, box.x + 2, box.y + 2, label, labelColor, 100);
  return true;
}

// Čím dál je pohled, tím méně měst se vejde, aby zůstala vidět letadla.
void drawCities(const RenderContext &context, MapLabelPlacer &placer) {
  const bool fullNames = context.rangeKm <= 25.0f;
  const uint8_t maxTier = context.rangeKm <= 50.0f ? 3 : 2;

  // Česká města mají vlastní, hustší seznam; kreslí se první, aby si u domova
  // zabrala místo dřív než evropská data, kde jsou tatáž místa řidčeji.
  for (uint8_t tier = 1; tier <= maxTier && tier <= 3; ++tier) {
    for (const CzechMapCity &city : CZECH_MAP_CITIES) {
      if (city.tier != tier) continue;
      if (city.latitude < context.southLatitude ||
          city.latitude > context.northLatitude ||
          city.longitude < context.westLongitude ||
          city.longitude > context.eastLongitude)
        continue;
      drawCity(context, placer, city.latitude, city.longitude,
               fullNames ? city.name : city.label);
    }
  }

  for (uint8_t tier = 1; tier <= maxTier; ++tier) {
    for (const EuCity &city : EU_CITIES) {
      if (city.tier != tier) continue;
      if (city.lat < context.southLatitude ||
          city.lat > context.northLatitude ||
          city.lon < context.westLongitude || city.lon > context.eastLongitude)
        continue;
      // Leží město ve vlastním českém seznamu? Porovnává se poloha, ne jméno:
      // evropská data píší některá jinak a podle jména by se nakreslila
      // dvakrát.
      bool alreadyDrawn = false;
      constexpr float TOLERANCE = 0.05f;  // zhruba pět kilometrů
      for (const CzechMapCity &czech : CZECH_MAP_CITIES) {
        if (fabsf(czech.latitude - city.lat) <= TOLERANCE &&
            fabsf(czech.longitude - city.lon) <= TOLERANCE) {
          alreadyDrawn = true;
          break;
        }
      }
      if (alreadyDrawn) continue;
      drawCity(context, placer, city.lat, city.lon,
               fullNames ? city.name : city.abbr);
    }
  }
}

// --- Letadla ----------------------------------------------------------------
// Barva ikony nese výšku, takže pohled stačí na rozeznání letadla na přiblížení
// od toho, které jen přelétá v hladině. Pásma na sebe navazují - každá výška
// padne přesně do jednoho.
uint16_t altitudeColor(float altitudeFt, bool known) {
  if (!known) return COLOR_UNKNOWN;
  const float kilometers = altitudeFt * 0.0003048f;
  if (kilometers < 2.0f) return COLOR_LOW;
  if (kilometers < 6.0f) return COLOR_MID;
  if (kilometers < 10.0f) return COLOR_HIGH;
  return COLOR_CRUISE;
}

// Ikona letadla: okřídlená šipka otočená po traťovém úhlu, vyplněná vějířem
// trojúhelníků ze středu. Neznámý směr se kreslí kolečkem - natočit ho není
// podle čeho.
void drawAircraftIcon(int x, int y, float trackDeg, bool hasTrack,
                      uint16_t color) {
  const uint16_t drawColor = paletteColor(color);
  if (!hasTrack) {
    drawMapCircle(pixels, x, y, 7, drawColor, 100);
    fillMapCircle(pixels, x, y, 2, drawColor, 100);
    return;
  }
  const float radians = trackDeg * DEGREES_TO_RADIANS;
  const float cosine = cosf(radians);
  const float sine = sinf(radians);
  // Místní souřadnice: right doprava od nosu, forward dopředu. Traťový úhel 0
  // míří nahoru, 90 doprava.
  const float shape[10][2] = {
      {0, 12}, {3, 1}, {13, -8}, {3, -5}, {3, -7},
      {0, -12}, {-3, -7}, {-3, -5}, {-13, -8}, {-3, 1}};
  int pointX[10];
  int pointY[10];
  for (int index = 0; index < 10; ++index) {
    const float right = shape[index][0];
    const float forward = shape[index][1];
    pointX[index] = x + static_cast<int>(right * cosine + forward * sine);
    pointY[index] = y + static_cast<int>(right * sine - forward * cosine);
  }
  for (int index = 0; index < 10; ++index) {
    const int next = (index + 1) % 10;
    fillMapTriangle(pixels, x, y, pointX[index], pointY[index], pointX[next],
                    pointY[next], drawColor, 100);
  }
}

// Filtr rozhoduje o tom, co je na mapě - a tím i o čísle na řádku s počtem
// letadel, protože to má říkat, kolik jich je vidět, ne kolik jich server
// poslal. Nedotčené zůstává hledání nouzového kódu a hlídaného letu: ta se
// dělají PŘED filtrem, takže nastavená výška je schovat nemůže. Kolik letadel
// přišlo celkem, ukazuje diagnostika.
bool passesFilter(const AdsbAircraft &aircraft,
                  const ClockPlanesConfig &planes) {
  if (planes.onlyWithCallsign && aircraft.callsign[0] == '\0') return false;
  if (planes.altitudeMinFt == 0 &&
      planes.altitudeMaxFt >= CLOCK_PLANE_ALTITUDE_CEILING_FT)
    return true;
  // Neznámou výšku filtr nezahazuje - není podle čeho ji zařadit.
  if (!aircraft.hasAltitude) return true;
  return aircraft.altitudeFt >= static_cast<float>(planes.altitudeMinFt) &&
         aircraft.altitudeFt <= static_cast<float>(planes.altitudeMaxFt);
}

// Hlídaný let. Porovnává se s callsignem i s ICAO adresou, protože lidé citují
// to, co zrovna mají.
bool isWatched(const AdsbAircraft &aircraft, const ClockPlanesConfig &planes) {
  if (planes.watchCallsign[0] == '\0') return false;
  if (aircraft.callsign[0] != '\0' &&
      strcasecmp(aircraft.callsign, planes.watchCallsign) == 0)
    return true;
  return aircraft.hex[0] != '\0' &&
         strcasecmp(aircraft.hex, planes.watchCallsign) == 0;
}

// --- Ozdoby ----------------------------------------------------------------
void drawRangeRingsAndCenter() {
  const uint16_t ringColor = paletteColor(COLOR_DARK_GRAY);
  const uint16_t crossColor = paletteColor(COLOR_WHITE);
  // Kružnice musí sedět s dosahem: vnější je přesně nastavený dosah, vnitřní
  // jeho polovina. Kreslit je podle šířky displeje znamenalo, že vnější ležela
  // za hranicí, kde se letadla ještě kreslí - mizela by uvnitř viditelného
  // kruhu.
  drawMapCircle(pixels, RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS, ringColor,
                100);
  drawMapCircle(pixels, RADAR_CENTER_X, RADAR_CENTER_Y, RADAR_RADIUS / 2,
                ringColor, 100);
  for (int offset = -8; offset <= 8; ++offset) {
    setMapPixel(pixels, RADAR_CENTER_X + offset, RADAR_CENTER_Y, crossColor,
                100);
    setMapPixel(pixels, RADAR_CENTER_X, RADAR_CENTER_Y + offset, crossColor,
                100);
  }
}

// Světové strany. Azimut b se objeví na displeji pod úhlem (b - topBearing).
void drawCompassMarks() {
  constexpr int MARK_RADIUS = 205;
  static const char *const LABELS[4] = {"S", "V", "J", "Z"};
  static const int BEARINGS[4] = {0, 90, 180, 270};
  const uint16_t color = paletteColor(COLOR_GRAY);
  for (int index = 0; index < 4; ++index) {
    const float angle = (BEARINGS[index] - static_cast<int>(rotationDegrees)) *
                        DEGREES_TO_RADIANS;
    const int x = RADAR_CENTER_X + static_cast<int>(MARK_RADIUS * sinf(angle)) - 2;
    const int y = RADAR_CENTER_Y - static_cast<int>(MARK_RADIUS * cosf(angle)) - 3;
    drawMapText(pixels, x, y, LABELS[index], color, 100);
  }
}

// Stupnice výšek. Barva ikony bez klíče nic neznamená: čtyři políčka v souvislém
// pruhu a pod ním hranice pásem (2 / 6 / 10 km).
void drawAltitudeLegend() {
  constexpr int SWATCH_WIDTH = 26;
  constexpr int BAR_WIDTH = 4 * SWATCH_WIDTH;
  constexpr int LEGEND_Y = 98;
  const int left = RADAR_CENTER_X - BAR_WIDTH / 2;
  const uint16_t colors[4] = {COLOR_LOW, COLOR_MID, COLOR_HIGH, COLOR_CRUISE};
  static const char *const BOUNDS[3] = {"2", "6", "10"};

  for (int index = 0; index < 4; ++index) {
    fillMapRect(pixels, left + index * SWATCH_WIDTH, LEGEND_Y, SWATCH_WIDTH, 6,
                paletteColor(colors[index]), 100);
  }
  const uint16_t textColor = paletteColor(COLOR_GRAY);
  for (int index = 0; index < 3; ++index) {
    const int edge = left + (index + 1) * SWATCH_WIDTH;
    drawMapText(pixels, edge - mapTextWidth(BOUNDS[index]) / 2, LEGEND_Y + 10,
                BOUNDS[index], textColor, 100);
  }
  drawMapText(pixels, left + BAR_WIDTH + 4, LEGEND_Y + 10, "KM", textColor, 100);
}

// Tečky dosahu pod popiskem dole, aby bylo vidět, kolik kroků ještě zbývá.
void drawRangeDots(uint8_t rangeIndex) {
  constexpr int DOT_GAP = 24;
  constexpr int DOT_Y = 432;
  const int totalWidth = (CLOCK_PLANE_RANGE_COUNT - 1) * DOT_GAP;
  const int startX = RADAR_CENTER_X - totalWidth / 2;
  for (uint8_t index = 0; index < CLOCK_PLANE_RANGE_COUNT; ++index) {
    const int x = startX + index * DOT_GAP;
    if (index == rangeIndex)
      fillMapCircle(pixels, x, DOT_Y, 5, paletteColor(COLOR_HIGH), 100);
    else
      drawMapCircle(pixels, x, DOT_Y, 5, paletteColor(COLOR_GRAY), 100);
  }
}

// Pásma, která si drží LVGL popisky obrazovky. Rezervují se PŘED mapou:
// popisky měst a callsignů se umisťují podle dat, ne podle návrhu, takže musí
// prohrát - a prohrát můžou jen tehdy, když je místo zabrané dřív, než si o ně
// řeknou.
void reserveChromeBands(MapLabelPlacer &placer) {
  // Tečky obrazovek, hodiny, řádek s počtem letadel, stupnice výšek, dosah
  // a tečky dosahu. Pásma se nesmí překrývat: rozmisťovač odmítne obálku, která
  // do nějaké zabrané zasahuje, takže by se druhé z dvojice vůbec nezarezervovalo.
  const MapLabelBox bands[] = {
      {0, 18, PLANE_RADAR_WIDTH, 22},   // tečky obrazovek
      {0, 40, PLANE_RADAR_WIDTH, 28},   // čas
      {0, 69, PLANE_RADAR_WIDTH, 26},   // počet letadel / nouzový stav
      {0, 96, PLANE_RADAR_WIDTH, 24},   // stupnice výšek
      {0, 388, PLANE_RADAR_WIDTH, 34},  // dosah
      {0, 424, PLANE_RADAR_WIDTH, 20},  // tečky dosahu
  };
  for (const MapLabelBox &band : bands) placer.claim(band);
}

// --- Vykreslení snímku ------------------------------------------------------
// Kreslí se z úlohy radaru do bufferu v PSRAM; obrazovka si ho pak jen podloží
// pod canvas. Volá se se zkopírovaným požadavkem, ne pod zámkem.
void renderFrame(const ClockPlanesConfig &planes, float latitude,
                 float longitude, bool night) {
  if (displayBuffers[0] == nullptr || displayBuffers[1] == nullptr ||
      liveList == nullptr) {
    return;
  }
  // Kreslí se do toho bufferu, který zrovna není na displeji. Buffer se nejdřív
  // celý přemaže černou a kreslení mezitím uvolňuje procesor, takže by LVGL nad
  // sdíleným bufferem ukázal rozdělanou mapu.
  // Kreslí se do bufferu, který nedrží ani obrazovka, ani poslední zveřejněný
  // snímek. Když jsou obsazené oba - zveřejnilo se, ale obrazovka si to ještě
  // nevyzvedla - snímek se přeskočí a zkusí se hned v dalším průchodu; jinak by
  // se přemazal buffer, který si za chvíli odnese LVGL.
  portENTER_CRITICAL(&stateMux);
  int target = -1;
  for (int index = 0; index < static_cast<int>(DISPLAY_BUFFER_COUNT); ++index) {
    if (index == displayedBuffer || index == handedOutBuffer) continue;
    target = index;
    break;
  }
  if (target < 0) redrawRequested = true;
  portEXIT_CRITICAL(&stateMux);
  if (target < 0) return;
  pixels = displayBuffers[target];
  nightPalette = night;
  setRotation(planes.topBearingDeg);

  RenderContext context;
  context.centerLatitude = latitude;
  context.centerLongitude = longitude;
  context.rangeKm = rangeKmForIndex(planes.rangeIndex);
  // Kruh má poloměr `range` km, takže viditelný rozsah je `range` na každou
  // stranu; dvacetiprocentní rezerva nechá projít i čáry, které jen zavadí
  // o okraj.
  const float marginKm = context.rangeKm * 1.2f;
  const float deltaLatitude = marginKm / KM_PER_DEGREE_LATITUDE;
  const float cosineLatitude = cosf(latitude * DEGREES_TO_RADIANS);
  const float deltaLongitude =
      marginKm / (KM_PER_DEGREE_LATITUDE *
                  (fabsf(cosineLatitude) < 0.01f ? 0.01f : cosineLatitude));
  context.southLatitude = latitude - deltaLatitude;
  context.northLatitude = latitude + deltaLatitude;
  context.westLongitude = longitude - deltaLongitude;
  context.eastLongitude = longitude + deltaLongitude;

  for (size_t index = 0; index < PIXEL_COUNT; ++index)
    pixels[index] = paletteColor(COLOR_BLACK);

  MapLabelPlacer placer;
  reserveChromeBands(placer);
  drawEuropeBorders(context);
  drawCzechBorder(context);
  drawCities(context, placer);
  drawRangeRingsAndCenter();
  drawCompassMarks();

  // Kopie stavu, se kterou se kreslí. Seznam letadel mění jen tahle úloha,
  // takže se nemusí zamykat; výběr a hlídaný let ano.
  char selection[8];
  portENTER_CRITICAL(&stateMux);
  strlcpy(selection, selectedHex, sizeof(selection));
  const uint8_t count = aircraftCount;
  portEXIT_CRITICAL(&stateMux);

  const int selectedIndex = adsbFindByHex(liveList, count, selection);
  const char *worstEmergency = nullptr;
  uint8_t worstEmergencySeverity = 0;
  bool watchedSeen = false;
  uint8_t drawn = 0;
  PlanePoint points[ADSB_MAX_AIRCRAFT];
  uint8_t pointCount = 0;

  for (uint8_t index = 0; index < count; ++index) {
    const AdsbAircraft &aircraft = liveList[index];
    // Nouzový stav a hlídaný let se hledají PŘED filtrem, aby je nastavená
    // výška nemohla schovat.
    const char *emergency =
        planes.squawkAlert ? adsbEmergencyCode(aircraft) : nullptr;
    const bool watched = isWatched(aircraft, planes);
    // Na hlášku je jediný řádek, takže na něm musí skončit ten nejhorší stav,
    // ne ten, který se v seznamu náhodou objevil první.
    const uint8_t severity = adsbEmergencySeverity(emergency);
    if (severity > worstEmergencySeverity) {
      worstEmergencySeverity = severity;
      worstEmergency = emergency;
    }
    if (watched) watchedSeen = true;
    if (emergency == nullptr && !watched && !passesFilter(aircraft, planes))
      continue;

    int x = 0;
    int y = 0;
    projectInContext(context, aircraft.latitude, aircraft.longitude, x, y);
    if (!insideCircle(x, y, RADAR_RADIUS)) continue;

    points[pointCount].x = static_cast<int16_t>(x);
    points[pointCount].y = static_cast<int16_t>(y);
    strlcpy(points[pointCount].hex, aircraft.hex, sizeof(points[pointCount].hex));
    ++pointCount;

    // Vybrané letadlo dostane bílý kroužek: barva ikony teď nese výšku a
    // azurová seděla příliš blízko modré hladinové.
    if (static_cast<int>(index) == selectedIndex)
      drawMapCircle(pixels, x, y, 16, paletteColor(COLOR_WHITE), 100);
    // Nouze červený kroužek, hlídaný let zelený. Oba širší než výběr, aby byly
    // vidět na první pohled.
    if (emergency != nullptr) {
      drawMapCircle(pixels, x, y, 20, paletteColor(COLOR_LOW), 100);
      drawMapCircle(pixels, x, y, 21, paletteColor(COLOR_LOW), 100);
    } else if (watched) {
      drawMapCircle(pixels, x, y, 20, paletteColor(COLOR_WATCHED), 100);
      drawMapCircle(pixels, x, y, 21, paletteColor(COLOR_WATCHED), 100);
    }

    const bool altitudeKnown = aircraft.hasAltitude;
    // Mapa je otočená, takže se musí stejně opravit i směr ikony - jinak by
    // letadlo mířilo špatně.
    // Zbytek po dělení, ne smyčka: traťový úhel je číslo od cizího serveru a
    // pro dostatečně velké záporné by se přičítání po 360 stupních netrefilo do
    // nuly nikdy - úloha radaru by se zasekla a hlídací pes by hodiny
    // restartoval.
    float screenTrack =
        fmodf(aircraft.trackDeg - static_cast<float>(rotationDegrees), 360.0f);
    if (screenTrack < 0.0f) screenTrack += 360.0f;
    if (!isfinite(screenTrack)) screenTrack = 0.0f;
    drawAircraftIcon(x, y, screenTrack, aircraft.hasTrack,
                     altitudeColor(aircraft.altitudeFt, altitudeKnown));

    // Popisek pod ikonou: callsign, nebo ICAO adresa, když ho letadlo nevysílá.
    // Náhrada žije JEN tady - AdsbAircraft::callsign zůstává schválně prázdný,
    // protože se posílá do API na trasu a adresa se tam čte jako číslo letu.
    const char *label =
        aircraft.callsign[0] != '\0' ? aircraft.callsign : aircraft.hex;
    if (label[0] != '\0') {
      const int textWidth = mapTextWidth(label);
      const MapLabelBox box = {x - textWidth / 2 - 2, y + 20, textWidth + 4, 13};
      if (placer.claim(box)) {
        const uint16_t labelColor =
            emergency != nullptr ? COLOR_LOW
                                 : (watched ? COLOR_WATCHED : COLOR_WHITE);
        drawMapText(pixels, box.x + 2, box.y + 3, label,
                    paletteColor(labelColor), 100);
      }
    }
    ++drawn;
  }

  drawAltitudeLegend();
  drawRangeDots(planes.rangeIndex);

  portENTER_CRITICAL(&stateMux);
  memcpy(planePoints, points, sizeof(PlanePoint) * pointCount);
  planePointCount = pointCount;
  shownCount = drawn;
  watchedVisible = watchedSeen;
  strlcpy(frameEmergency, worstEmergency != nullptr ? worstEmergency : "",
          sizeof(frameEmergency));
  // Hotový snímek se zveřejní až tady, jedním přepnutím indexu.
  displayedBuffer = target;
  ++generation;
  ready = true;
  portEXIT_CRITICAL(&stateMux);
}

// --- Stahování --------------------------------------------------------------
// Jeden GET přes společný svazek kořenů. Vrací počet bajtů těla, nebo -1.
long downloadJson(const char *url, uint8_t *buffer, size_t capacity,
                  int &httpStatus) {
  httpStatus = 0;
  long result = -1;
  {
    WiFiClientSecure client;
    client.setCACertBundle(
        rootca_crt_bundle_start,
        static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
    client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    HTTPClient http;
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(RESPONSE_TIMEOUT_MS);
    // Bez tohoto si HTTPClient::end() spojení schová pro další použití a
    // nezavolá na klientovi stop(), takže kontexty mbedTLS včetně dvou
    // šestnáctikilobajtových bufferů zůstanou navždy alokované.
    http.setReuse(false);
    // User-Agent MUSÍ jít přes setUserAgent(): addHeader() ho tiše zahodí.
    http.setUserAgent(USER_AGENT);
    httpDownloadPrepare(http);
    if (http.begin(client, url)) {
      http.addHeader("Accept", "application/json");
      httpStatus = http.GET();
      if (httpStatus == HTTP_CODE_OK) {
        BoundedBufferStream response(buffer, capacity - 1);
        // Ne writeToStream(): u chunked odpovědi se nemusí nikdy vrátit a drží
        // TLS relaci i s interní RAM až do restartu.
        const int bytesRead =
            httpDownloadBody(http, response, RESPONSE_TIMEOUT_MS);
        if (!response.overflowed() && bytesRead >= 0) {
          buffer[response.length()] = '\0';
          result = static_cast<long>(response.length());
        }
      }
      http.end();
    }
    // Pojistka pro případ, že spojení vůbec nevzniklo nebo skončilo chybou.
    client.stop();
  }
  // Arduino core připojuje svazek kořenů při každém spojení, ale nikdy ho
  // neodpojí. Bez tohoto každé stažení ukousne kus interní RAM.
  esp_crt_bundle_detach(nullptr);
  return result;
}

bool fetchAircraft(const ClockPlanesConfig &planes, float latitude,
                   float longitude) {
  const float rangeKm = rangeKmForIndex(planes.rangeIndex);
  const float distanceNm = rangeKm / KM_PER_NAUTICAL_MILE;
  char url[128];
  snprintf(url, sizeof(url), "%s%.5f/lon/%.5f/dist/%.1f", ADSB_HOST,
           static_cast<double>(latitude), static_cast<double>(longitude),
           static_cast<double>(distanceNm));

  int httpStatus = 0;
  long length = -1;
  {
    NetworkOperationGuard guard(NETWORK_GUARD_MS);
    if (!guard) {
      setStatusMessage("Síť je zaneprázdněná");
      return false;
    }
    length = downloadJson(url, responseBuffer, MAX_RESPONSE_BYTES, httpStatus);
  }

  portENTER_CRITICAL(&stateMux);
  lastHttpStatus = httpStatus;
  lastDownloadedBytes = length > 0 ? static_cast<size_t>(length) : 0;
  portEXIT_CRITICAL(&stateMux);

  if (length < 0) {
    // Předchozí snímek zůstává na obrazovce - prázdná obloha po jednom
    // nepovedeném stažení vypadá jako pravda, ale není.
    setStatusMessage(httpStatus == HTTP_CODE_OK || httpStatus == 0
                         ? "Data letadel nyní nejsou dostupná"
                         : "Server letadel odmítl dotaz");
    return false;
  }

  const AdsbParseOutcome outcome = adsbParseAircraft(
      reinterpret_cast<const char *>(responseBuffer), scratchList,
      ADSB_MAX_AIRCRAFT);
  if (outcome.status != AdsbParseStatus::Ok) {
    portENTER_CRITICAL(&stateMux);
    strlcpy(serverMessage, outcome.message, sizeof(serverMessage));
    portEXIT_CRITICAL(&stateMux);
    setStatusMessage(outcome.status == AdsbParseStatus::NotJson
                         ? "Neočekávaná odpověď serveru letadel"
                         : "Server letadel neposlal letadla");
    return false;
  }

  // Hotový rozbor se překlopí do živého seznamu naráz, takže useknutá nebo
  // rozbitá odpověď radar nevymaže. Prohodí se ukazatele, ne obsah: kopie přes
  // čtyři kilobajty by se nedala udělat pod zámkem a výběr klepnutím, který
  // seznam čte z jiné úlohy, by v ní mohl chytit letadlo rozepsané do půlky.
  portENTER_CRITICAL(&stateMux);
  AdsbAircraft *parsed = scratchList;
  scratchList = liveList;
  liveList = parsed;
  aircraftCount = static_cast<uint8_t>(outcome.count);
  strlcpy(serverMessage, outcome.message, sizeof(serverMessage));
  lastSuccessAt = millis();
  portEXIT_CRITICAL(&stateMux);
  setStatusMessage("");
  return true;
}

// Trasa vybraného letu. Ptá se jen na jedno letadlo, nikdy na celý seznam, a
// odpověď se drží, takže přepínání mezi dvěma letadly už API nezatěžuje.
void fetchRouteIfPending(float aircraftLatitude, float aircraftLongitude) {
  char callsign[10];
  portENTER_CRITICAL(&stateMux);
  const bool pending = routePending;
  strlcpy(callsign, routeCallsign, sizeof(callsign));
  portEXIT_CRITICAL(&stateMux);
  if (!pending || callsign[0] == '\0') return;
  // Callsign jde do adresy dotazu a přišel z cizí odpovědi, takže se pouští jen
  // to, co callsign opravdu je. Cokoli jiného by adresu rozbilo nebo do ní
  // propašovalo další cestu; skutečné volací značky jsou písmena a číslice.
  for (const char *c = callsign; *c != '\0'; ++c) {
    const bool allowed = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                         (*c >= '0' && *c <= '9');
    if (allowed) continue;
    portENTER_CRITICAL(&stateMux);
    if (routePending && strcmp(routeCallsign, callsign) == 0) {
      routePending = false;
      routeKnown = false;
      redrawRequested = true;
    }
    portEXIT_CRITICAL(&stateMux);
    return;
  }

  char url[160];
  snprintf(url, sizeof(url), "%s%s/%.4f/%.4f", ROUTE_HOST, callsign,
           static_cast<double>(aircraftLatitude),
           static_cast<double>(aircraftLongitude));

  int httpStatus = 0;
  long length = -1;
  {
    NetworkOperationGuard guard(NETWORK_GUARD_MS);
    if (!guard) return;  // zkusí se znovu při dalším průchodu
    length = downloadJson(url, responseBuffer, MAX_ROUTE_RESPONSE_BYTES,
                          httpStatus);
  }

  RouteInfo parsed;
  RouteParseStatus status = RouteParseStatus::Invalid;
  if (length >= 0) {
    status = routeParse(reinterpret_cast<const char *>(responseBuffer),
                        aircraftLatitude, aircraftLongitude, parsed);
  }

  portENTER_CRITICAL(&stateMux);
  lastRouteHttpStatus = httpStatus;
  // Mezitím se mohlo vybrat jiné letadlo; odpověď pak patří někomu jinému.
  if (routePending && strcmp(routeCallsign, callsign) == 0) {
    routePending = false;
    routeKnown = (status == RouteParseStatus::Ok);
    routeInfo = routeKnown ? parsed : RouteInfo{};
    // Nepovedené stažení není totéž co "tenhle let trasu nemá". Zapamatovaný
    // callsign drží další pokus stranou, takže se po chybě sítě zahodí - jinak
    // by se u vybraného letadla trasa nezkusila načíst už nikdy.
    if (status == RouteParseStatus::Invalid) routeCallsign[0] = '\0';
    redrawRequested = true;
  }
  portEXIT_CRITICAL(&stateMux);
}

// --- Úloha ------------------------------------------------------------------
bool currentRequest(ClockPlanesConfig &planes, float &latitude,
                    float &longitude, bool &night, bool &wantVisible,
                    bool &wantActive) {
  portENTER_CRITICAL(&stateMux);
  planes = requestPlanes;
  latitude = requestLatitude;
  longitude = requestLongitude;
  night = redNightMode;
  wantVisible = visible;
  wantActive = active;
  portEXIT_CRITICAL(&stateMux);
  return wantActive;
}

// Po stahování se srovná výběr: letadlo, které v datech chybí několikrát po
// sobě, se pustí. Dělá se to JEDNOU ZA STAHOVÁNÍ, ne při kreslení - to běží
// mnohokrát za vteřinu a lhůtu by spálilo v mžiku.
void reconcileSelection() {
  portENTER_CRITICAL(&stateMux);
  if (selectedHex[0] == '\0') {
    portEXIT_CRITICAL(&stateMux);
    return;
  }
  const int index = adsbFindByHex(liveList, aircraftCount, selectedHex);
  if (index >= 0) {
    selectionMissCount = 0;
    selectionCache = liveList[index];
    selectionCacheValid = true;
    // Trasa se ptá až tady, kdy je známý callsign - klepnutí zná jen adresu.
    if (!routePending && !routeKnown &&
        selectionCache.callsign[0] != '\0' &&
        strcmp(routeCallsign, selectionCache.callsign) != 0) {
      strlcpy(routeCallsign, selectionCache.callsign, sizeof(routeCallsign));
      routePending = true;
    }
  } else if (++selectionMissCount > DETAIL_GRACE_POLLS) {
    selectedHex[0] = '\0';
    selectionMissCount = 0;
    selectionCacheValid = false;
    routeCallsign[0] = '\0';
    routePending = false;
    routeKnown = false;
    routeInfo = RouteInfo{};
  }
  portEXIT_CRITICAL(&stateMux);
}

void planeRadarTask(void *) {
  ClockPlanesConfig planes;
  float latitude = 0.0f;
  float longitude = 0.0f;
  bool night = false;
  bool wantVisible = false;
  bool wantActive = false;
  bool lastNight = false;
  uint32_t lastRevision = 0xffffffff;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    if (!currentRequest(planes, latitude, longitude, night, wantVisible,
                        wantActive)) {
      continue;
    }
    // Bez času ze sítě by TLS odmítlo každý certifikát jako "ještě neplatný".
    if (WiFi.status() != WL_CONNECTED || time(nullptr) < VALID_TIME_THRESHOLD) {
      portENTER_CRITICAL(&stateMux);
      nextFetchAt = millis() + RETRY_INTERVAL_MS;
      portEXIT_CRITICAL(&stateMux);
      setStatusMessage("Čekám na síť");
      continue;
    }
    if (!ensureStorage()) {
      setStatusMessage("Pro radar letadel není dostatek PSRAM");
      continue;
    }

    uint32_t revision = 0;
    unsigned long dueAt = 0;
    bool fetchNow = false;
    bool forceRedraw = false;
    portENTER_CRITICAL(&stateMux);
    revision = requestRevision;
    dueAt = nextFetchAt;
    fetchNow = fetchNowRequested;
    fetchNowRequested = false;
    forceRedraw = redrawRequested;
    redrawRequested = false;
    portEXIT_CRITICAL(&stateMux);

    // Změna nastavení (dosah, azimut, filtr) překreslí hned a stáhne znovu,
    // protože se s ní mění i to, na co se serveru ptáme.
    const bool settingsChanged = revision != lastRevision;
    if (settingsChanged) {
      lastRevision = revision;
      fetchNow = true;
    }

    const bool nightChanged = night != lastNight;
    lastNight = night;

    bool fetched = false;
    if (fetchNow || static_cast<long>(millis() - dueAt) >= 0) {
      portENTER_CRITICAL(&stateMux);
      loading = true;
      portEXIT_CRITICAL(&stateMux);
      const bool ok = fetchAircraft(planes, latitude, longitude);
      const uint32_t period = fetchPeriodMs(planes, wantVisible);
      portENTER_CRITICAL(&stateMux);
      loading = false;
      // Po neúspěchu se čeká dvojnásobek, ať se API nemlátí v normálním tempu.
      nextFetchAt = millis() + (ok ? period : period * 2);
      portEXIT_CRITICAL(&stateMux);
      if (ok) reconcileSelection();
      fetched = true;
    }

    // Trasa se stahuje mimo pořadí letadel, obvykle do vteřiny po klepnutí.
    float selectedLatitude = 0.0f;
    float selectedLongitude = 0.0f;
    bool wantRoute = false;
    portENTER_CRITICAL(&stateMux);
    if (routePending && selectionCacheValid) {
      selectedLatitude = selectionCache.latitude;
      selectedLongitude = selectionCache.longitude;
      wantRoute = true;
    }
    portEXIT_CRITICAL(&stateMux);
    if (wantRoute) fetchRouteIfPending(selectedLatitude, selectedLongitude);

    // Schovaná obrazovka si data drží, ale 230 tisíc pixelů kvůli nim
    // přepisovat nemusí - kreslí se tedy hlavně když je vidět.
    //
    // Jednu výjimku má: dokud není hotový vůbec první snímek, kreslí se i
    // schovaná. Automatická rotace obrazovku otevře teprve tehdy, když má co
    // ukázat, a bez tohohle by čekala na snímek, který by se bez otevření nikdy
    // nevykreslil - obrazovka by se do střídání nedostala nikdy.
    portENTER_CRITICAL(&stateMux);
    const bool redrawPending = redrawRequested;
    redrawRequested = false;
    const bool haveFirstFrame = ready;
    portEXIT_CRITICAL(&stateMux);
    const bool wantRedraw = fetched || settingsChanged || nightChanged ||
                            forceRedraw || redrawPending;
    if (wantRedraw && (wantVisible || !haveFirstFrame)) {
      renderFrame(planes, latitude, longitude, night);
    }
  }
}

}  // namespace

void planeRadarServiceBegin() {
  if (taskHandle != nullptr) return;
  xTaskCreatePinnedToCoreWithCaps(
      planeRadarTask, "plane-radar", 20480, nullptr, 1, &taskHandle, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void planeRadarServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  active = false;
  visible = false;
  ++requestRevision;
  portEXIT_CRITICAL(&stateMux);

  // OTA drží síťový koordinátor, takže radar teď nemůže být uvnitř HTTP ani
  // TLS operace. Ukončením úlohy zastavíme i případné kreslení, než uvolníme
  // její buffery.
  if (taskHandle != nullptr) {
    vTaskDeleteWithCaps(taskHandle);
    taskHandle = nullptr;
  }
  // Snímek se odhlásí dřív, než se uvolní jeho paměť: obrazovka ho čte z jiné
  // úlohy a mezi uvolněním a nulováním by sáhla do vráceného bufferu.
  portENTER_CRITICAL(&stateMux);
  displayedBuffer = -1;
  handedOutBuffer = -1;
  ready = false;
  loading = false;
  aircraftCount = 0;
  ++generation;
  portEXIT_CRITICAL(&stateMux);
  releaseStorage();
}

void planeRadarServiceSetActive(bool nowVisible, bool backgroundRefresh,
                                float latitude, float longitude,
                                const ClockPlanesConfig &planes) {
  bool notify = false;
  portENTER_CRITICAL(&stateMux);
  const bool wasActive = active;
  // Dosah si služba drží sama, dokud ho někdo nezmění na webu: přetažení prstem
  // mění jen běžící dosah a předání konfigurace ho nesmí srazit zpátky.
  const uint8_t runningRangeIndex = requestPlanes.rangeIndex;
  const bool configuredRangeChanged = planes.rangeIndex != configuredRangeIndex;
  configuredRangeIndex = planes.rangeIndex;
  // Změna toho, na co se serveru ptáme, musí úlohu probudit a stáhnout znovu.
  const bool requestChanged =
      configuredRangeChanged ||
      requestPlanes.refreshSeconds != planes.refreshSeconds ||
      requestPlanes.topBearingDeg != planes.topBearingDeg ||
      requestPlanes.altitudeMinFt != planes.altitudeMinFt ||
      requestPlanes.altitudeMaxFt != planes.altitudeMaxFt ||
      requestPlanes.onlyWithCallsign != planes.onlyWithCallsign ||
      requestPlanes.squawkAlert != planes.squawkAlert ||
      strcmp(requestPlanes.watchCallsign, planes.watchCallsign) != 0 ||
      requestLatitude != latitude || requestLongitude != longitude;
  requestPlanes = planes;
  if (!configuredRangeChanged) requestPlanes.rangeIndex = runningRangeIndex;
  requestLatitude = latitude;
  requestLongitude = longitude;
  const bool wasVisible = visible;
  visible = nowVisible;
  active = nowVisible || backgroundRefresh;
  // Stáhnout hned je potřeba nejen při zapnutí služby, ale i při každém
  // otevření obrazovky: na pozadí se čeká pět minut mezi dotazy, takže by na
  // radar šla poloha letadel stará až tolik, a to po celou dobu, co je vidět.
  if (requestChanged || (active && !wasActive) || (nowVisible && !wasVisible)) {
    ++requestRevision;
    fetchNowRequested = true;
    notify = true;
  }
  if (nowVisible) {
    redrawRequested = true;
    notify = true;
  }
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void planeRadarServiceSetRedNightMode(bool enabled) {
  portENTER_CRITICAL(&stateMux);
  if (redNightMode == enabled) {
    portEXIT_CRITICAL(&stateMux);
    return;
  }
  redNightMode = enabled;
  redrawRequested = visible;
  const bool notify = redrawRequested;
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

void planeRadarServiceSnapshot(PlaneRadarSnapshot &snapshot) {
  portENTER_CRITICAL(&stateMux);
  snapshot.pixels =
      displayedBuffer >= 0 ? displayBuffers[displayedBuffer] : nullptr;
  // Obrazovka si ukazatel odnáší; od téhle chvíle se do toho bufferu nekreslí.
  handedOutBuffer = displayedBuffer;
  snapshot.generation = generation;
  snapshot.loading = loading;
  snapshot.ready = ready;
  snapshot.shownCount = shownCount;
  snapshot.watchedVisible = watchedVisible;
  strlcpy(snapshot.emergency, frameEmergency, sizeof(snapshot.emergency));
  snapshot.rangeKm =
      CLOCK_PLANE_RANGES_KM[requestPlanes.rangeIndex < CLOCK_PLANE_RANGE_COUNT
                                ? requestPlanes.rangeIndex
                                : 1];
  strlcpy(snapshot.message, statusMessage, sizeof(snapshot.message));

  PlaneRadarDetail &detail = snapshot.detail;
  detail = PlaneRadarDetail{};
  detail.open = selectedHex[0] != '\0';
  if (detail.open && selectionCacheValid) {
    const AdsbAircraft &aircraft = selectionCache;
    detail.signalLost = selectionMissCount > 0;
    strlcpy(detail.callsign, aircraft.callsign, sizeof(detail.callsign));
    strlcpy(detail.hex, aircraft.hex, sizeof(detail.hex));
    strlcpy(detail.type, aircraft.type, sizeof(detail.type));
    strlcpy(detail.registration, aircraft.registration,
            sizeof(detail.registration));
    strlcpy(detail.squawk, aircraft.squawk, sizeof(detail.squawk));
    const char *emergency = adsbEmergencyCode(aircraft);
    strlcpy(detail.emergency, emergency != nullptr ? emergency : "",
            sizeof(detail.emergency));
    detail.hasTrack = aircraft.hasTrack;
    detail.trackDeg = aircraft.trackDeg;
    detail.altitudeFt = aircraft.altitudeFt;
    detail.groundSpeedKt = aircraft.groundSpeedKt;
    detail.verticalRateFtMin = aircraft.verticalRateFtMin;
    detail.routePending = routePending;
    detail.routeKnown = routeKnown;
    detail.route = routeInfo;
  } else {
    detail.open = false;
  }
  portEXIT_CRITICAL(&stateMux);
}

void planeRadarServiceDiagnostics(PlaneRadarDiagnostics &diagnostics) {
  portENTER_CRITICAL(&stateMux);
  diagnostics.available = requestPlanes.enabled;
  diagnostics.active = active;
  diagnostics.visible = visible;
  diagnostics.loading = loading;
  diagnostics.ready = ready;
  diagnostics.aircraftCount = aircraftCount;
  diagnostics.rangeKm =
      CLOCK_PLANE_RANGES_KM[requestPlanes.rangeIndex < CLOCK_PLANE_RANGE_COUNT
                                ? requestPlanes.rangeIndex
                                : 1];
  const unsigned long now = millis();
  diagnostics.lastSuccessfulRefreshAgeMs =
      lastSuccessAt == 0 ? 0 : static_cast<uint32_t>(now - lastSuccessAt);
  diagnostics.nextRefreshInMs =
      (!fetchNowRequested && static_cast<long>(nextFetchAt - now) > 0)
          ? static_cast<uint32_t>(nextFetchAt - now)
          : 0;
  diagnostics.lastHttpStatus = lastHttpStatus;
  diagnostics.lastDownloadedBytes = lastDownloadedBytes;
  diagnostics.lastRouteHttpStatus = lastRouteHttpStatus;
  strlcpy(diagnostics.message, statusMessage, sizeof(diagnostics.message));
  strlcpy(diagnostics.serverMessage, serverMessage,
          sizeof(diagnostics.serverMessage));
  portEXIT_CRITICAL(&stateMux);
}

bool planeRadarServiceHandleTap(int16_t x, int16_t y) {
  bool changed = false;
  portENTER_CRITICAL(&stateMux);
  if (selectedHex[0] != '\0') {
    // S otevřeným detailem zavírá kterékoli klepnutí.
    selectedHex[0] = '\0';
    selectionMissCount = 0;
    selectionCacheValid = false;
    routeCallsign[0] = '\0';
    routePending = false;
    routeKnown = false;
    routeInfo = RouteInfo{};
    changed = true;
  } else {
    // Nejbližší letadlo do třiceti pixelů. Zapamatuje se ADRESA, ne pozice
    // v poli - to už za chvíli znamená něco jiného.
    int best = -1;
    long bestDistance = 30L * 30L;
    for (uint8_t index = 0; index < planePointCount; ++index) {
      const long deltaX = planePoints[index].x - x;
      const long deltaY = planePoints[index].y - y;
      const long distance = deltaX * deltaX + deltaY * deltaY;
      if (distance < bestDistance) {
        bestDistance = distance;
        best = index;
      }
    }
    // Vybrat jde jen letadlo, které je pořád v datech. Zapsat adresu i bez něj
    // by nechalo detail "otevřený" bez čeho ukázat: panel by se nekreslil, ale
    // dosah by se nedal přetáhnout a další klepnutí by se spotřebovalo na
    // zavření něčeho, co není vidět.
    const int found =
        best >= 0 && planePoints[best].hex[0] != '\0'
            ? adsbFindByHex(liveList, aircraftCount, planePoints[best].hex)
            : -1;
    if (found >= 0) {
      strlcpy(selectedHex, planePoints[best].hex, sizeof(selectedHex));
      selectionMissCount = 0;
      selectionCache = liveList[found];
      selectionCacheValid = true;
      routeInfo = RouteInfo{};
      routeKnown = false;
      routeCallsign[0] = '\0';
      routePending = false;
      // Na trasu se ptá jen letadlo, které volací značku opravdu vysílá:
      // hexadecimální adresa se v tom API čte jako číslo letu.
      if (selectionCache.callsign[0] != '\0') {
        strlcpy(routeCallsign, selectionCache.callsign, sizeof(routeCallsign));
        routePending = true;
      }
      changed = true;
    }
  }
  if (changed) redrawRequested = true;
  portEXIT_CRITICAL(&stateMux);
  if (changed && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
  return changed;
}

void planeRadarServiceChangeRange(int8_t direction) {
  bool notify = false;
  portENTER_CRITICAL(&stateMux);
  // S otevřeným detailem se dosah nemění - nejdřív se zavírá.
  if (selectedHex[0] == '\0') {
    // Na krajích se zastaví, nepřeskočí dokola - stejně jako dosah
    // meteoradaru. Skok ze sta kilometrů rovnou na deset by navíc rozjel
    // zbytečné stažení.
    const int next = static_cast<int>(requestPlanes.rangeIndex) + direction;
    if (next >= 0 && next < CLOCK_PLANE_RANGE_COUNT &&
        next != requestPlanes.rangeIndex) {
      requestPlanes.rangeIndex = static_cast<uint8_t>(next);
      ++requestRevision;
      fetchNowRequested = true;
      redrawRequested = true;
      notify = true;
    }
  }
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

bool planeRadarServiceDetailOpen() {
  portENTER_CRITICAL(&stateMux);
  const bool open = selectedHex[0] != '\0';
  portEXIT_CRITICAL(&stateMux);
  return open;
}

void planeRadarServiceCloseDetail() {
  bool notify = false;
  portENTER_CRITICAL(&stateMux);
  if (selectedHex[0] != '\0') {
    selectedHex[0] = '\0';
    selectionMissCount = 0;
    selectionCacheValid = false;
    routeCallsign[0] = '\0';
    routePending = false;
    routeKnown = false;
    routeInfo = RouteInfo{};
    redrawRequested = true;
    notify = true;
  }
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}
