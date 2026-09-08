#include "WeatherForecastService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <string.h>
#include <time.h>

#include "NetworkCoordinator.h"
#include "FirmwareHubCa.h"

namespace {
// Obě adresy patří Open-Meteo.
constexpr char FORECAST_HOST[] = "https://api.open-meteo.com/v1/forecast";
constexpr char AIR_QUALITY_HOST[] =
    "https://air-quality-api.open-meteo.com/v1/air-quality";

constexpr uint32_t FORECAST_CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t FORECAST_RESPONSE_TIMEOUT_MS = 15000;
constexpr uint32_t FORECAST_NETWORK_GUARD_MS = 15000;
// Pět dní po hodinách má kolem 3,5 kB; strop je s rezervou na delší názvy
// časových pásem i na to, že by Open-Meteo přidal desetinné místo.
constexpr size_t FORECAST_MAX_RESPONSE_BYTES = 12 * 1024;

// Kolik dní si říct. Obrazovka ukazuje nanejvýš čtyři následující, dnešek
// popisují hodiny nad nimi - proto o jeden víc.
constexpr int FORECAST_REQUESTED_DAYS = WEATHER_FORECAST_MAX_DAYS + 1;

StaticSemaphore_t forecastMutexStorage;
SemaphoreHandle_t forecastMutex = nullptr;

// Mezipaměť leží v PSRAM: WeatherForecastData má přes půl kilobajtu a úloha,
// která zrovna drží TLS relaci, si ji na zásobník dovolit nemůže.
struct ForecastCache {
  WeatherForecastData data;
  // Rozebraná odpověď před převzetím do data, aby neúspěšné stažení nesmazalo
  // předpověď, kterou obrazovka právě ukazuje.
  WeatherForecastData scratch;
  uint32_t generation;
  bool ready;
  bool failed;
};

ForecastCache *forecastCache = nullptr;
char *forecastBuffer = nullptr;
bool forecastLoading = false;
// Stahování běží mimo zámek, aby smyčka displeje mohla dál číst starou
// předpověď. Po tu dobu se buffer nesmí uvolnit, takže požadavek na zahození
// počká na dokončení stahování.
bool forecastFetchActive = false;
volatile bool forecastClearPending = false;
unsigned long forecastLastSuccessAt = 0;

void releaseStorage() {
  if (forecastCache != nullptr) {
    // Ne `*forecastCache = ForecastCache{}`: dočasná kopie struktury má přes
    // kilobajt a stála by tolik zásobníku volající úloze.
    const uint32_t generation = forecastCache->generation;
    memset(forecastCache, 0, sizeof(*forecastCache));
    forecastCache->generation = generation + 1;
  }
  if (forecastBuffer != nullptr) {
    heap_caps_free(forecastBuffer);
    forecastBuffer = nullptr;
  }
  forecastLastSuccessAt = 0;
}

ForecastCache *ensureCache() {
  if (forecastCache == nullptr) {
    forecastCache = static_cast<ForecastCache *>(heap_caps_calloc(
        1, sizeof(ForecastCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return forecastCache;
}

char *ensureBuffer() {
  if (forecastBuffer == nullptr) {
    forecastBuffer = static_cast<char *>(heap_caps_malloc(
        FORECAST_MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return forecastBuffer;
}

// Stáhne tělo odpovědi do bufferu a ukončí ho nulou. Vrací počet bajtů, nebo
// -1 při chybě. HTTP/1.0 vynutí odpověď bez chunked rámování, takže se čte
// prostě do konce spojení - u těla pod dvanáct kilobajtů je to nejlevnější.
int downloadJson(const char *url, char *buffer, size_t capacity,
                 int &httpStatus) {
  httpStatus = HTTPC_ERROR_CONNECTION_REFUSED;
  WiFiClientSecure client;
  client.setCACert(FIRMWARE_RELEASE_ROOT_CA);
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(FORECAST_CONNECT_TIMEOUT_MS);
  http.setTimeout(FORECAST_RESPONSE_TIMEOUT_MS);
  // Bez tohoto si HTTPClient::end() spojení schová pro další použití a
  // nezavolá na klientovi stop(), takže kontexty mbedTLS zůstanou alokované.
  http.setReuse(false);
  int length = -1;
  if (http.begin(client, url)) {
    httpStatus = http.GET();
    if (httpStatus == HTTP_CODE_OK) {
      size_t received = 0;
      WiFiClient *stream = http.getStreamPtr();
      int remaining = http.getSize();
      unsigned long lastDataAt = millis();
      while ((remaining > 0 || remaining == -1) && received + 1 < capacity) {
        const size_t available = stream->available();
        if (available == 0) {
          const unsigned long idleFor = millis() - lastDataAt;
          if (idleFor > FORECAST_RESPONSE_TIMEOUT_MS ||
              (!http.connected() && idleFor > 750)) {
            break;
          }
          delay(2);
          continue;
        }
        const size_t wanted =
            available < capacity - 1 - received ? available
                                                : capacity - 1 - received;
        const int bytesRead = stream->readBytes(buffer + received, wanted);
        if (bytesRead <= 0) break;
        received += static_cast<size_t>(bytesRead);
        lastDataAt = millis();
        if (remaining > 0) remaining -= bytesRead;
      }
      buffer[received] = '\0';
      // Useknuté tělo se nerozebírá: rozebralo by se jako kratší předpověď
      // a vypadalo by to jako správně načtená data. Useknutí poznají dvě věci
      // - server slíbil delší obsah, než kolik dorazilo (spadlé spojení nebo
      // ticho do timeoutu), anebo se buffer zaplnil až po okraj a zbytek
      // odpovědi se do něj nevešel. Ptát se místo toho, jestli ve streamu
      // ještě něco je, nestačí: v ten okamžik nemusí být došlé nic, i když
      // server dál posílá.
      const bool truncated = remaining > 0 || received + 1 >= capacity;
      length = truncated || received == 0 ? -1 : static_cast<int>(received);
    }
    http.end();
  }
  // Pojistka pro případ, že spojení vůbec nevzniklo nebo skončilo chybou:
  // uvolnění TLS kontextů se nesmí spoléhat na destruktor, který ho nedělá.
  client.stop();
  return length;
}

// Nejstarší hodina, která se ještě smí ukázat: začátek té právě probíhající.
// Open-Meteo vydává hodinová razítka jako skutečný čas UTC, takže se porovnají
// přímo s time().
int64_t firstHourToShow(time_t now) {
  return static_cast<int64_t>(now) - static_cast<int64_t>(now % 3600);
}

// První den, který se ještě smí ukázat: zítřek. Dnešek popisují hodiny nad
// denní částí, takže by v ní byl jen podruhé. Denní razítka jsou půlnoc
// místního času vyjádřená v UTC, což mktime() nad místním tm vrací také.
int64_t firstDayToShow(time_t now) {
  struct tm local;
  if (localtime_r(&now, &local) == nullptr) return 0;
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  // mktime() si posun přes konec měsíce i letní čas srovná samo.
  local.tm_mday += 1;
  local.tm_isdst = -1;
  const time_t midnight = mktime(&local);
  return midnight == static_cast<time_t>(-1) ? 0
                                             : static_cast<int64_t>(midnight);
}

void buildForecastUrl(float latitude, float longitude, String &url) {
  url = FORECAST_HOST;
  url += F("?latitude=");
  url += String(latitude, 5);
  url += F("&longitude=");
  url += String(longitude, 5);
  url += F(
      "&hourly=temperature_2m,precipitation,weather_code,wind_speed_10m,"
      "is_day&daily=weather_code,temperature_2m_max,temperature_2m_min,"
      "precipitation_sum,wind_speed_10m_max&timeformat=unixtime&timezone=auto"
      "&forecast_days=");
  url += FORECAST_REQUESTED_DAYS;
}

void buildAirQualityUrl(float latitude, float longitude, String &url) {
  url = AIR_QUALITY_HOST;
  url += F("?latitude=");
  url += String(latitude, 5);
  url += F("&longitude=");
  url += String(longitude, 5);
  url += F(
      "&current=european_aqi,pm2_5,grass_pollen&timeformat=unixtime"
      "&timezone=auto");
}

}  // namespace

void weatherForecastServiceBegin() {
  if (forecastMutex == nullptr)
    forecastMutex = xSemaphoreCreateMutexStatic(&forecastMutexStorage);
}

void weatherForecastServiceStatus(WeatherForecastStatus &status) {
  status = WeatherForecastStatus{};
  if (forecastMutex == nullptr ||
      xSemaphoreTake(forecastMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return;
  }
  status.loading = forecastLoading;
  if (forecastLastSuccessAt != 0) {
    status.lastSuccessAvailable = true;
    status.lastSuccessAgeMs =
        static_cast<uint32_t>(millis() - forecastLastSuccessAt);
  }
  if (forecastCache != nullptr) {
    status.generation = forecastCache->generation;
    status.ready = forecastCache->ready;
    status.failed = forecastCache->failed;
    status.hourCount = forecastCache->data.hourCount;
  }
  xSemaphoreGive(forecastMutex);
}

bool weatherForecastServiceSnapshot(WeatherForecastData &forecast) {
  if (forecastMutex == nullptr ||
      xSemaphoreTake(forecastMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  const bool ready = forecastCache != nullptr && forecastCache->ready;
  if (ready) forecast = forecastCache->data;
  xSemaphoreGive(forecastMutex);
  return ready;
}

bool weatherForecastServiceFetch(float latitude, float longitude,
                                 bool airQuality,
                                 NetworkDiagnosticKind diagnosticKind) {
  networkDiagnosticsBegin(diagnosticKind);
  if (WiFi.status() != WL_CONNECTED) {
    networkDiagnosticsSetDetail(diagnosticKind, F("Wi-Fi není připojená."));
    networkDiagnosticsEnd(diagnosticKind, false,
                          HTTPC_ERROR_CONNECTION_REFUSED);
    return false;
  }
  NetworkOperationGuard networkGuard(FORECAST_NETWORK_GUARD_MS);
  if (!networkGuard) {
    networkDiagnosticsSetDetail(diagnosticKind,
                                F("Síť je právě vytížená jinou operací."));
    networkDiagnosticsEnd(diagnosticKind, false,
                          HTTPC_ERROR_CONNECTION_REFUSED);
    return false;
  }

  if (forecastMutex == nullptr ||
      xSemaphoreTake(forecastMutex, portMAX_DELAY) != pdTRUE) {
    networkDiagnosticsSetDetail(diagnosticKind, F("Zámek předpovědi selhal."));
    networkDiagnosticsEnd(diagnosticKind, false,
                          HTTPC_ERROR_CONNECTION_REFUSED);
    return false;
  }
  forecastLoading = true;
  forecastFetchActive = true;
  ForecastCache *cache = ensureCache();
  char *buffer = ensureBuffer();
  const bool storageReady = cache != nullptr && buffer != nullptr;
  if (!storageReady) {
    forecastLoading = false;
    forecastFetchActive = false;
    // Úklid odložený na dobu stahování se nesmí ztratit ani tady.
    if (forecastClearPending) {
      forecastClearPending = false;
      releaseStorage();
    }
  }
  xSemaphoreGive(forecastMutex);
  if (!storageReady) {
    networkDiagnosticsSetDetail(diagnosticKind,
                                F("Pro předpověď není dostatek PSRAM."));
    networkDiagnosticsEnd(diagnosticKind, false,
                          HTTPC_ERROR_CONNECTION_REFUSED);
    return false;
  }

  String url;
  buildForecastUrl(latitude, longitude, url);
  int httpStatus = HTTPC_ERROR_CONNECTION_REFUSED;
  const bool downloaded =
      downloadJson(url.c_str(), buffer, FORECAST_MAX_RESPONSE_BYTES,
                   httpStatus) > 0;
  const time_t now = time(nullptr);
  bool parsed = false;
  if (downloaded) {
    parsed = weatherForecastParse(buffer, firstHourToShow(now),
                                  firstDayToShow(now),
                                  WEATHER_FORECAST_MAX_HOURS,
                                  WEATHER_FORECAST_MAX_DAYS, cache->scratch);
  }

  // Kvalita ovzduší je doplněk: bez ní se předpověď pořád ukáže, jen bez
  // spodní sekce. Neúspěch tady tedy celé stažení neshodí.
  if (parsed && airQuality) {
    String airUrl;
    buildAirQualityUrl(latitude, longitude, airUrl);
    int airStatus = HTTPC_ERROR_CONNECTION_REFUSED;
    if (downloadJson(airUrl.c_str(), buffer, FORECAST_MAX_RESPONSE_BYTES,
                     airStatus) > 0) {
      cache->scratch.airAvailable =
          weatherForecastParseAirQuality(buffer, cache->scratch.air);
    }
  }

  if (forecastMutex == nullptr ||
      xSemaphoreTake(forecastMutex, portMAX_DELAY) != pdTRUE) {
    networkDiagnosticsSetDetail(diagnosticKind, F("Zámek předpovědi selhal."));
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  forecastLoading = false;
  forecastFetchActive = false;
  // Obrazovka se během stahování vypnula nebo se přesunulo město. Výsledek
  // patří jinému nastavení, takže se zahodí i s mezipamětí.
  if (forecastClearPending) {
    forecastClearPending = false;
    releaseStorage();
    xSemaphoreGive(forecastMutex);
    networkDiagnosticsSetDetail(diagnosticKind,
                                F("Předpověď se mezitím vypnula."));
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  if (parsed) {
    cache->data = cache->scratch;
    cache->ready = true;
    cache->failed = false;
    forecastLastSuccessAt = millis();
  } else {
    // Stará předpověď zůstane na obrazovce; jen se poznamená, že se ji
    // nepodařilo obnovit.
    cache->failed = true;
  }
  ++cache->generation;
  xSemaphoreGive(forecastMutex);

  networkDiagnosticsSetDetail(
      diagnosticKind,
      parsed ? F("Předpověď načtena")
             : downloaded ? F("Odpověď neobsahuje předpověď")
                          : F("Předpověď nyní není dostupná"));
  networkDiagnosticsEnd(diagnosticKind, parsed, httpStatus);
  return parsed;
}

void weatherForecastServiceClear() {
  if (forecastMutex == nullptr ||
      xSemaphoreTake(forecastMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  if (forecastFetchActive) {
    // Buffer právě plní probíhající stažení; uvolní ho samo, až skončí.
    forecastClearPending = true;
  } else {
    releaseStorage();
  }
  xSemaphoreGive(forecastMutex);
}
