#include "AgendaService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <string.h>

#include "HttpDownload.h"
#include "NetworkCoordinator.h"

// Kořenové certifikáty Mozilly slinkované v mbedTLS. Adresu serveru zadává
// uživatel, takže připnout jeden kořen jako u ostatních služeb nejde - stejně
// jako u kanálu se zprávami.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
// Stejné meze jako u kanálu se zprávami. Stahování drží síť výhradně pro sebe,
// takže delší timeouty by rozbíjely souběžné čtení Open-Meteo i Home Assistantu.
constexpr uint32_t AGENDA_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t AGENDA_RESPONSE_TIMEOUT_MS = 8000;
// Naměřená odpověď dvanácti událostí má kolem kilobajtu. Strop je s velkou
// rezervou pro server nastavený na delší okno, ale pořád tak nízko, aby
// chybová stránka místo JSONu skončila u parseru, ne v paměti.
constexpr size_t AGENDA_MAX_RESPONSE_BYTES = 16 * 1024;
constexpr uint32_t AGENDA_NETWORK_GUARD_MS = 10000;

StaticSemaphore_t agendaMutexStorage;
SemaphoreHandle_t agendaMutex = nullptr;

struct AgendaCache {
  AgendaFeed feed;
  // Rozebraná agenda před převzetím do feed. AgendaFeed má přes 1,6 kB, což je
  // na zásobník úlohy provádějící TLS handshake příliš. Souběh nehrozí: celé
  // stahování drží NetworkOperationGuard, takže běží vždy jen jedno.
  AgendaFeed scratch;
  uint32_t generation;
  bool ready;
  char message[AGENDA_MESSAGE_LENGTH];
  // Výsledek poslední zkoušky adresy z webu. Leží mimo feed, aby zkoušená
  // adresa nepřepsala události, které hodiny právě ukazují.
  AgendaFeed probe;
  bool probeReady;
};

AgendaCache *agendaCache = nullptr;
uint8_t *agendaBuffer = nullptr;
bool agendaLoading = false;
// Stahování běží mimo zámek, aby smyčka displeje mohla dál číst starý obsah.
// Po tu dobu se mezipaměť ani buffer nesmí uvolnit, takže požadavek na
// zahození počká na dokončení stahování.
bool agendaFetchActive = false;
volatile bool agendaClearPending = false;
// millis() posledního úspěšného stažení, nebo 0, když mezipaměť žádné nemá.
unsigned long agendaLastSuccessAt = 0;

void releaseStorage() {
  if (agendaCache != nullptr) {
    // Ne `*agendaCache = AgendaCache{}`: dočasná kopie struktury má přes 5 kB
    // a stála by tolik zásobníku úloze, která zrovna drží TLS relaci.
    const uint32_t generation = agendaCache->generation;
    memset(agendaCache, 0, sizeof(*agendaCache));
    agendaCache->generation = generation + 1;
  }
  if (agendaBuffer != nullptr) {
    heap_caps_free(agendaBuffer);
    agendaBuffer = nullptr;
  }
  agendaLastSuccessAt = 0;
}

AgendaCache *ensureCache() {
  if (agendaCache == nullptr) {
    agendaCache = static_cast<AgendaCache *>(heap_caps_calloc(
        1, sizeof(AgendaCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return agendaCache;
}

uint8_t *ensureBuffer() {
  if (agendaBuffer == nullptr) {
    agendaBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        AGENDA_MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return agendaBuffer;
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
  strlcpy(destination, value.c_str(), AGENDA_MESSAGE_LENGTH);
}

}  // namespace

void agendaServiceBegin() {
  if (agendaMutex == nullptr) {
    agendaMutex = xSemaphoreCreateMutexStatic(&agendaMutexStorage);
  }
}

bool agendaServiceStatus(AgendaStatus &status) {
  status = AgendaStatus{};
  if (agendaMutex == nullptr ||
      xSemaphoreTake(agendaMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  status.loading = agendaLoading;
  if (agendaLastSuccessAt != 0) {
    status.lastSuccessAvailable = true;
    status.lastSuccessAgeMs =
        static_cast<uint32_t>(millis() - agendaLastSuccessAt);
  }
  if (agendaCache != nullptr) {
    status.generation = agendaCache->generation;
    status.count = agendaCache->feed.count;
    status.ready = agendaCache->ready;
    strlcpy(status.message, agendaCache->message, sizeof(status.message));
  }
  xSemaphoreGive(agendaMutex);
  return true;
}

bool agendaServiceProbeStatus(AgendaProbeStatus &status) {
  status = AgendaProbeStatus{};
  if (agendaMutex == nullptr ||
      xSemaphoreTake(agendaMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  if (agendaCache != nullptr && agendaCache->probeReady) {
    status.ready = true;
    status.count = agendaCache->probe.count;
  }
  xSemaphoreGive(agendaMutex);
  return true;
}

bool agendaServiceVisitItems(AgendaItemVisitor visitor, void *context) {
  if (visitor == nullptr) return false;
  if (agendaMutex == nullptr ||
      xSemaphoreTake(agendaMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  if (agendaCache != nullptr && agendaCache->ready) {
    for (size_t index = 0; index < agendaCache->feed.count; ++index) {
      const AgendaItem &source = agendaCache->feed.items[index];
      const AgendaDisplayItem item{source.day, source.time, source.title,
                                   source.calendar};
      visitor(index, item, context);
    }
  }
  xSemaphoreGive(agendaMutex);
  return true;
}

bool agendaServiceVisitProbeItems(AgendaItemVisitor visitor, void *context) {
  if (visitor == nullptr) return false;
  if (agendaMutex == nullptr ||
      xSemaphoreTake(agendaMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  if (agendaCache != nullptr && agendaCache->probeReady) {
    for (size_t index = 0; index < agendaCache->probe.count; ++index) {
      const AgendaItem &source = agendaCache->probe.items[index];
      const AgendaDisplayItem item{source.day, source.time, source.title,
                                   source.calendar};
      visitor(index, item, context);
    }
  }
  xSemaphoreGive(agendaMutex);
  return true;
}

// Společné tělo běžného stažení i zkoušky z webu. Liší se jen tím, kam se
// rozebraná agenda uloží: běžné stažení převezme obrazovka, zkouška zůstane
// stranou v probe, aby zkoušená adresa nepřepsala zobrazené události.
static bool agendaServiceDownload(const ClockAgendaConfig &config,
                                  NetworkDiagnosticKind diagnosticKind,
                                  bool probeOnly, int &httpStatus,
                                  String &error) {
  httpStatus = HTTPC_ERROR_CONNECTION_REFUSED;
  error = "";
  if (config.url[0] == '\0') {
    error = F("Adresa agendy není vyplněná.");
    return false;
  }
  const bool secure = strncmp(config.url, "https://", 8) == 0;
  if (!secure && strncmp(config.url, "http://", 7) != 0) {
    error = F("Adresa musí začínat http:// nebo https://.");
    return false;
  }

  networkDiagnosticsBegin(diagnosticKind);
  NetworkOperationGuard networkGuard(AGENDA_NETWORK_GUARD_MS);
  if (!networkGuard) {
    error = F("Síť je právě vytížená jinou operací.");
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }

  if (agendaMutex == nullptr ||
      xSemaphoreTake(agendaMutex, portMAX_DELAY) != pdTRUE) {
    error = F("Agenda nyní není dostupná.");
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  agendaLoading = true;
  agendaFetchActive = true;
  AgendaCache *cache = ensureCache();
  uint8_t *buffer = ensureBuffer();
  if (cache == nullptr || buffer == nullptr) {
    agendaLoading = false;
    agendaFetchActive = false;
    // Úklid odložený na dobu stahování se nesmí ztratit ani tady.
    if (agendaClearPending) {
      agendaClearPending = false;
      releaseStorage();
    }
    error = F("Pro agendu není dostatek PSRAM.");
    xSemaphoreGive(agendaMutex);
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  xSemaphoreGive(agendaMutex);

  size_t payloadLength = 0;
  {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    // Adresu serveru zadává uživatel, takže se certifikát ověřuje proti svazku
    // kořenů Mozilly; připnout jeden kořen jako u ostatních služeb nejde.
    if (secure) {
      secureClient.setCACertBundle(
          rootca_crt_bundle_start,
          static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
      secureClient.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    }
    HTTPClient http;
    http.setConnectTimeout(AGENDA_CONNECT_TIMEOUT_MS);
    http.setTimeout(AGENDA_RESPONSE_TIMEOUT_MS);
    // Bez tohoto si HTTPClient::end() spojení schová pro další použití a
    // nezavolá na klientovi stop(), takže kontexty mbedTLS včetně dvou
    // šestnáctikilobajtových bufferů zůstanou navždy alokované. Viz stejná
    // poznámka v RssService.cpp - stálo to přes 50 kB interní RAM.
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(F("WaveshareHodiny"));
    WiFiClient &client = secure ? static_cast<WiFiClient &>(secureClient)
                                : plainClient;
    httpDownloadPrepare(http);
    if (http.begin(client, config.url)) {
      httpStatus = http.GET();
      if (httpStatus == HTTP_CODE_OK) {
        const int declaredSize = http.getSize();
        if (declaredSize > static_cast<int>(AGENDA_MAX_RESPONSE_BYTES)) {
          error = F("Odpověď serveru je příliš velká.");
        } else {
          BoundedBufferStream response(buffer, AGENDA_MAX_RESPONSE_BYTES);
          // Ne writeToStream(): u chunked odpovědi se nikdy nevrátila a držela
          // TLS relaci i s interní RAM až do restartu.
          const int bytesRead =
              httpDownloadBody(http, response, AGENDA_RESPONSE_TIMEOUT_MS);
          payloadLength = response.length();
          if (response.overflowed()) {
            error = F("Odpověď serveru je příliš velká.");
          } else if (bytesRead < 0) {
            // Useknuté tělo se nerozebírá: rozebralo by se jako kratší agenda
            // a vypadalo by to jako správně načtené události.
            error = F("Agenda nyní není dostupná.");
          }
        }
      }
      http.end();
    }
    // Pojistka pro případ, že spojení vůbec nevzniklo nebo skončilo chybou:
    // uvolnění TLS kontextů se nesmí spoléhat na destruktor, který ho nedělá.
    client.stop();
  }
  // Arduino core 3.0.7 připojuje svazek kořenů při každém spojení, ale
  // stop_ssl_socket() ho nikdy neodpojí. Bez tohoto odpojení každé stažení
  // ukousne kus interní RAM. Odpojuje se až po zániku klienta.
  if (secure) esp_crt_bundle_detach(nullptr);

  if (error.isEmpty() && httpStatus != HTTP_CODE_OK) {
    if (httpStatus == HTTP_CODE_NOT_FOUND) {
      error = F("Na zadané adrese agenda není.");
    } else if (httpStatus == HTTP_CODE_SERVICE_UNAVAILABLE) {
      // 503 posílá serve.py, dokud generátor poprvé nedoběhl.
      error = F("Server agendu ještě nepřipravil.");
    } else {
      error = F("Agenda nyní není dostupná.");
    }
  }

  AgendaFeed &parsed = cache->scratch;
  if (error.isEmpty()) {
    const AgendaParseOutcome outcome =
        agendaParseFeed(reinterpret_cast<const char *>(buffer), payloadLength,
                        config.itemCount, parsed);
    if (outcome.status == AgendaParseStatus::NotJson) {
      error = F("Odpověď serveru není JSON.");
    } else if (outcome.status == AgendaParseStatus::MissingArray) {
      error = F("Odpověď serveru nemá seznam událostí.");
    }
  }

  if (agendaMutex == nullptr ||
      xSemaphoreTake(agendaMutex, portMAX_DELAY) != pdTRUE) {
    networkDiagnosticsSetDetail(diagnosticKind, F("Zámek agendy selhal."));
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  agendaLoading = false;
  agendaFetchActive = false;
  // Agenda se během stahování vypnula nebo změnila adresu. Výsledek patří
  // jinému zdroji, takže se zahodí i s bufferem.
  if (agendaClearPending) {
    agendaClearPending = false;
    releaseStorage();
    xSemaphoreGive(agendaMutex);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  const bool ok = error.isEmpty();
  if (probeOnly) {
    // Zkouška se drží stranou: ani při úspěchu se nemění feed, generation ani
    // agendaLastSuccessAt, takže obrazovka dál ukazuje uloženou agendu.
    cache->probeReady = ok;
    if (ok) {
      cache->probe = parsed;
      String detail = F("Zkouška načetla událostí: ");
      detail += parsed.count;
      networkDiagnosticsSetDetail(diagnosticKind, detail);
    } else {
      networkDiagnosticsSetDetail(diagnosticKind, error);
    }
  } else if (ok) {
    // Prázdná agenda je platný výsledek, ne chyba: den bez události je běžný
    // stav. Proto se ready nastaví i při nule a obrazovka řekne "nic nemáš"
    // místo hlášky o nedostupném serveru.
    cache->feed = parsed;
    cache->ready = true;
    cache->message[0] = '\0';
    ++cache->generation;
    agendaLastSuccessAt = millis();
    String detail = F("Načteno událostí: ");
    detail += parsed.count;
    networkDiagnosticsSetDetail(diagnosticKind, detail);
  } else {
    // Poslední úspěšný obsah zůstává na obrazovce; hláška se ukáže jen tehdy,
    // když ještě žádný nebyl.
    copyMessage(cache->message, error);
    if (!cache->ready) ++cache->generation;
    networkDiagnosticsSetDetail(diagnosticKind, error);
  }
  xSemaphoreGive(agendaMutex);
  networkDiagnosticsEnd(diagnosticKind, ok, httpStatus);
  return ok;
}

bool agendaServiceFetch(const ClockAgendaConfig &config,
                        NetworkDiagnosticKind diagnosticKind, int &httpStatus,
                        String &error) {
  return agendaServiceDownload(config, diagnosticKind, false, httpStatus,
                               error);
}

bool agendaServiceProbe(const ClockAgendaConfig &config, int &httpStatus,
                        String &error) {
  return agendaServiceDownload(config, NetworkDiagnosticKind::AgendaTest, true,
                               httpStatus, error);
}

void agendaServiceClear() {
  // Volá se z úlohy agendy mezi stahováními, ale sáhnout si sem může i
  // agendaServiceProbe, která běží ve stejné úloze. Samotné stahování zámek
  // nedrží, takže úklid v takovém případě převezme ono.
  if (agendaMutex == nullptr ||
      xSemaphoreTake(agendaMutex, portMAX_DELAY) != pdTRUE) {
    agendaClearPending = true;
    return;
  }
  if (agendaFetchActive) {
    agendaClearPending = true;
    xSemaphoreGive(agendaMutex);
    return;
  }
  agendaClearPending = false;
  releaseStorage();
  agendaLoading = false;
  xSemaphoreGive(agendaMutex);
}
