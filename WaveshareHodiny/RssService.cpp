#include "RssService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <string.h>
#include <time.h>

#include "HttpDownload.h"
#include "NetworkCoordinator.h"

// Kořenové certifikáty Mozilly slinkované v mbedTLS. Adresu kanálu zadává
// uživatel, takže připnout jeden kořen jako u ostatních služeb nejde.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
// Stejné meze jako u TMEP. Kanál drží síť výhradně pro sebe, takže delší
// timeouty by rozbíjely souběžné čtení Open-Meteo i Home Assistantu.
constexpr uint32_t RSS_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t RSS_RESPONSE_TIMEOUT_MS = 8000;
// Kanál iROZHLAS.cz má kolem 18 kB. Strop je s velkou rezervou, protože jiné
// redakce posílají v description celé články.
constexpr size_t RSS_MAX_RESPONSE_BYTES = 160 * 1024;
constexpr uint32_t RSS_NETWORK_GUARD_MS = 10000;

StaticSemaphore_t rssMutexStorage;
SemaphoreHandle_t rssMutex = nullptr;

struct RssCache {
  RssFeed feed;
  // Rozebraný kanál před převzetím do feed. RssFeed má přes 1,4 kB, což je na
  // zásobník úlohy provádějící TLS handshake příliš. Souběh nehrozí: celé
  // stahování drží NetworkOperationGuard, takže běží vždy jen jedno.
  RssFeed scratch;
  char times[RSS_MAX_ITEMS][RSS_TIME_LENGTH];
  uint32_t generation;
  bool ready;
  char message[RSS_MESSAGE_LENGTH];
};

RssCache *rssCache = nullptr;
uint8_t *rssBuffer = nullptr;
bool rssLoading = false;
// Stahování běží mimo zámek, aby smyčka displeje mohla dál číst starý obsah.
// Po tu dobu se mezipaměť ani buffer nesmí uvolnit, takže požadavek na
// zahození počká na dokončení stahování.
bool rssFetchActive = false;
volatile bool rssClearPending = false;
// millis() posledního úspěšného stažení, nebo 0, když mezipaměť žádné nemá.
unsigned long rssLastSuccessAt = 0;

void releaseStorage() {
  if (rssCache != nullptr) {
    // Ne `*rssCache = RssCache{}`: dočasná kopie struktury má přes 3 kB a
    // stála by tolik zásobníku úloze, která zrovna drží TLS relaci.
    const uint32_t generation = rssCache->generation;
    memset(rssCache, 0, sizeof(*rssCache));
    rssCache->generation = generation + 1;
  }
  if (rssBuffer != nullptr) {
    heap_caps_free(rssBuffer);
    rssBuffer = nullptr;
  }
  rssLastSuccessAt = 0;
}

RssCache *ensureCache() {
  if (rssCache == nullptr) {
    rssCache = static_cast<RssCache *>(heap_caps_calloc(
        1, sizeof(RssCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return rssCache;
}

uint8_t *ensureBuffer() {
  if (rssBuffer == nullptr) {
    rssBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        RSS_MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return rssBuffer;
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
  uint8_t *buffer_;
  size_t capacity_;
  size_t length_ = 0;
  bool overflowed_ = false;
};

void copyMessage(char *destination, const String &value) {
  strlcpy(destination, value.c_str(), RSS_MESSAGE_LENGTH);
}

// Čas vydání na místní "HH:MM". Bez synchronizovaného času by vyšel rok 1970,
// což by na obrazovce vypadalo jako platný, ale nesmyslný údaj.
void formatLocalTime(int64_t publishedAt, bool available, char *destination) {
  destination[0] = '\0';
  if (!available) return;
  const time_t stamp = static_cast<time_t>(publishedAt);
  struct tm local;
  if (localtime_r(&stamp, &local) == nullptr) return;
  if (local.tm_year + 1900 < 2020) return;
  snprintf(destination, RSS_TIME_LENGTH, "%02d:%02d", local.tm_hour,
           local.tm_min);
}

}  // namespace

void rssServiceBegin() {
  if (rssMutex == nullptr) rssMutex = xSemaphoreCreateMutexStatic(&rssMutexStorage);
}

void rssServiceStatus(RssStatus &status) {
  status = RssStatus{};
  if (rssMutex == nullptr ||
      xSemaphoreTake(rssMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return;
  }
  status.loading = rssLoading;
  if (rssLastSuccessAt != 0) {
    status.lastSuccessAvailable = true;
    status.lastSuccessAgeMs =
        static_cast<uint32_t>(millis() - rssLastSuccessAt);
  }
  if (rssCache != nullptr) {
    status.generation = rssCache->generation;
    status.count = rssCache->feed.count;
    status.ready = rssCache->ready;
    strlcpy(status.channelTitle, rssCache->feed.channelTitle,
            sizeof(status.channelTitle));
    strlcpy(status.message, rssCache->message, sizeof(status.message));
  }
  xSemaphoreGive(rssMutex);
}

bool rssServiceVisitItems(RssItemVisitor visitor, void *context) {
  if (visitor == nullptr) return false;
  if (rssMutex == nullptr ||
      xSemaphoreTake(rssMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  if (rssCache != nullptr && rssCache->ready) {
    for (size_t index = 0; index < rssCache->feed.count; ++index) {
      const RssDisplayItem item{rssCache->feed.items[index].title,
                                rssCache->times[index]};
      visitor(index, item, context);
    }
  }
  xSemaphoreGive(rssMutex);
  return true;
}

bool rssServiceFetch(const ClockRssConfig &config,
                     NetworkDiagnosticKind diagnosticKind, int &httpStatus,
                     String &error) {
  httpStatus = HTTPC_ERROR_CONNECTION_REFUSED;
  error = "";
  if (config.url[0] == '\0') {
    error = F("Adresa kanálu není vyplněná.");
    return false;
  }
  const bool secure = strncmp(config.url, "https://", 8) == 0;
  if (!secure && strncmp(config.url, "http://", 7) != 0) {
    error = F("Adresa musí začínat http:// nebo https://.");
    return false;
  }

  networkDiagnosticsBegin(diagnosticKind);
  NetworkOperationGuard networkGuard(RSS_NETWORK_GUARD_MS);
  if (!networkGuard) {
    error = F("Síť je právě vytížená jinou operací.");
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }

  if (rssMutex == nullptr ||
      xSemaphoreTake(rssMutex, portMAX_DELAY) != pdTRUE) {
    error = F("Kanál zpráv nyní není dostupný.");
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  rssLoading = true;
  rssFetchActive = true;
  RssCache *cache = ensureCache();
  uint8_t *buffer = ensureBuffer();
  if (cache == nullptr || buffer == nullptr) {
    rssLoading = false;
    rssFetchActive = false;
    // Úklid odložený na dobu stahování se nesmí ztratit ani tady.
    if (rssClearPending) {
      rssClearPending = false;
      releaseStorage();
    }
    error = F("Pro kanál zpráv není dostatek PSRAM.");
    xSemaphoreGive(rssMutex);
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  xSemaphoreGive(rssMutex);

  size_t payloadLength = 0;
  {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    // Adresu kanálu zadává uživatel, takže se certifikát ověřuje proti svazku
    // kořenů Mozilly; připnout jeden kořen jako u ostatních služeb nejde.
    // Svazek paměť nestojí: dřívější propad interní RAM po stažení
    // (87 -> 37 kB) nezpůsobilo ověřování ani TLS, ale zaseknuté
    // HTTPClient::writeToStream(), které nechalo relaci otevřenou.
    if (secure) {
      secureClient.setCACertBundle(
          rootca_crt_bundle_start,
          static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
    }
    HTTPClient http;
    http.setConnectTimeout(RSS_CONNECT_TIMEOUT_MS);
    http.setTimeout(RSS_RESPONSE_TIMEOUT_MS);
    // Bez tohoto si HTTPClient::end() spojení schová pro další použití
    // ("tcp keep open for reuse") a nezavolá na klientovi stop(). Destruktor
    // NetworkClientSecure ho nezavolá také, takže kontexty mbedTLS včetně
    // dvou šestnáctikilobajtových bufferů zůstanou navždy alokované. Na tomto
    // zařízení to znamená přes 50 kB interní RAM, o kterou pak přijde
    // Wi-Fi zásobník a přestane přenášet data.
    http.setReuse(false);
    // Zpravodajské servery běžně přesměrovávají na kanonickou adresu.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(F("WaveshareHodiny"));
    WiFiClient &client = secure ? static_cast<WiFiClient &>(secureClient)
                                : plainClient;
    httpDownloadPrepare(http);
    if (http.begin(client, config.url)) {
      httpStatus = http.GET();
      if (httpStatus == HTTP_CODE_OK) {
        const int declaredSize = http.getSize();
        if (declaredSize > static_cast<int>(RSS_MAX_RESPONSE_BYTES)) {
          error = F("Kanál je příliš velký.");
        } else {
          BoundedBufferStream response(buffer, RSS_MAX_RESPONSE_BYTES);
          // Ne writeToStream(): u chunked odpovědi iROZHLASu se nikdy
          // nevrátila a držela TLS relaci i s interní RAM až do restartu.
          const int bytesRead =
              httpDownloadBody(http, response, RSS_RESPONSE_TIMEOUT_MS);
          payloadLength = response.length();
          if (response.overflowed()) {
            error = F("Kanál je příliš velký.");
          } else if (bytesRead < 0) {
            // Useknuté tělo se nerozebírá: rozebralo by se jako kratší kanál a
            // vypadalo by to jako správně načtené zprávy.
            error = F("Kanál nyní není dostupný.");
          }
        }
      }
      http.end();
    }
    // Pojistka pro případ, že spojení vůbec nevzniklo nebo skončilo chybou:
    // uvolnění TLS kontextů se nesmí spoléhat na destruktor, který ho nedělá.
    client.stop();
  }
  // Arduino core 3.0.7 připojuje svazek kořenů při každém spojení
  // (ssl_client.cpp řádek 206), ale stop_ssl_socket() ho nikdy neodpojí -
  // uvolní jen struktury mbedTLS. Bez tohoto odpojení každé stažení kanálu
  // ukousne kus interní RAM, až na ni nezbude pro web server. Odpojuje se až
  // po zániku klienta, aby si ověřovací callback a uvolněný svazek nemohly
  // překážet.
  if (secure) esp_crt_bundle_detach(nullptr);

  if (error.isEmpty() && httpStatus != HTTP_CODE_OK) {
    error = httpStatus == HTTP_CODE_NOT_FOUND
                ? F("Kanál na zadané adrese neexistuje.")
                : F("Kanál nyní není dostupný.");
  }

  RssFeed &parsed = cache->scratch;
  char parseError[RSS_MESSAGE_LENGTH];
  parseError[0] = '\0';
  if (error.isEmpty() &&
      !rssParseFeed(reinterpret_cast<const char *>(buffer), payloadLength,
                    config.itemCount, parsed, parseError,
                    sizeof(parseError))) {
    error = parseError;
  }

  if (rssMutex == nullptr ||
      xSemaphoreTake(rssMutex, portMAX_DELAY) != pdTRUE) {
    networkDiagnosticsSetDetail(diagnosticKind, F("Zámek kanálu selhal."));
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  rssLoading = false;
  rssFetchActive = false;
  // Kanál se během stahování vypnul nebo změnil adresu. Výsledek patří jinému
  // zdroji, takže se zahodí i s bufferem.
  if (rssClearPending) {
    rssClearPending = false;
    releaseStorage();
    xSemaphoreGive(rssMutex);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  const bool ok = error.isEmpty();
  if (ok) {
    cache->feed = parsed;
    for (size_t index = 0; index < parsed.count; ++index) {
      formatLocalTime(parsed.items[index].publishedAt,
                      parsed.items[index].timeAvailable, cache->times[index]);
    }
    cache->ready = true;
    cache->message[0] = '\0';
    ++cache->generation;
    rssLastSuccessAt = millis();
    String detail = F("Načteno zpráv: ");
    detail += parsed.count;
    networkDiagnosticsSetDetail(diagnosticKind, detail);
  } else {
    // Poslední úspěšný obsah zůstává na obrazovce; hláška se ukáže jen tehdy,
    // když ještě žádný nebyl.
    copyMessage(cache->message, error);
    if (!cache->ready) ++cache->generation;
    networkDiagnosticsSetDetail(diagnosticKind, error);
  }
  xSemaphoreGive(rssMutex);
  networkDiagnosticsEnd(diagnosticKind, ok, httpStatus);
  return ok;
}

void rssServiceClear() {
  // Volá se z úlohy kanálu, zatímco stahovat může souběžně i webová zkouška
  // kanálu ve smyčce. Zámek se drží vždy jen po dobu kopírování, nikdy přes
  // síťové čtení, takže se na něj dá počkat. Samotné stahování ale zámek
  // nedrží, takže úklid v takovém případě převezme ono.
  if (rssMutex == nullptr ||
      xSemaphoreTake(rssMutex, portMAX_DELAY) != pdTRUE) {
    rssClearPending = true;
    return;
  }
  if (rssFetchActive) {
    rssClearPending = true;
    xSemaphoreGive(rssMutex);
    return;
  }
  rssClearPending = false;
  releaseStorage();
  rssLoading = false;
  xSemaphoreGive(rssMutex);
}
