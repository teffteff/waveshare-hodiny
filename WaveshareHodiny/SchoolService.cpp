#include "SchoolService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <string.h>

#include "HttpDownload.h"
#include "NetworkCoordinator.h"

// Kořenové certifikáty Mozilly slinkované v mbedTLS; adresu serveru zadává
// majitel, takže připnout jeden kořen nejde - stejně jako u agendy.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
// Stejné meze jako u agendy. Stahování drží síť výhradně pro sebe, takže
// delší timeouty by rozbíjely souběžné čtení ostatních služeb.
constexpr uint32_t SCHOOL_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t SCHOOL_RESPONSE_TIMEOUT_MS = 8000;
// Den s osmi hodinami a pěti úkoly má kolem dvou kilobajtů. Strop nechává
// rezervu, ale chybová stránka místo JSONu skončí u parseru, ne v paměti.
constexpr size_t SCHOOL_MAX_RESPONSE_BYTES = 16 * 1024;
constexpr uint32_t SCHOOL_NETWORK_GUARD_MS = 10000;
// Jak dlouho po posledním úspěchu smí obrazovka ukazovat uloženou kopii, když
// server neodpovídá. Popisky DNES a ZÍTRA v ní skládal server v době stažení,
// takže by jinak zůstaly stát. Tři hodiny pokryjí nejdelší obnovu (120 minut)
// i pár neúspěšných pokusů.
constexpr uint32_t SCHOOL_STALE_MS = 3UL * 60UL * 60UL * 1000UL;

StaticSemaphore_t schoolMutexStorage;
SemaphoreHandle_t schoolMutex = nullptr;

struct SchoolCache {
  SchoolFeed feed;
  // Rozebraný rozvrh před převzetím. Feed má přes čtyři kilobajty, což na
  // zásobník úlohy s TLS relací nepatří. Souběh nehrozí: stahování drží
  // NetworkOperationGuard.
  SchoolFeed scratch;
  SchoolFeed probe;
  uint32_t generation;
  bool ready;
  bool probeReady;
  char message[SCHOOL_MESSAGE_LENGTH];
};

SchoolCache *schoolCache = nullptr;
uint8_t *schoolBuffer = nullptr;
bool schoolLoading = false;
// Stahování běží mimo zámek, aby displej mohl dál číst starý rozvrh. Po tu
// dobu se mezipaměť nesmí uvolnit; úklid počká na konec stahování.
bool schoolFetchActive = false;
volatile bool schoolClearPending = false;
unsigned long schoolLastSuccessAt = 0;

void releaseStorage() {
  if (schoolCache != nullptr) {
    // Ne přiřazení dočasné struktury: ta by stála přes dvanáct kilobajtů
    // zásobníku úlohy, která zrovna drží TLS.
    const uint32_t generation = schoolCache->generation;
    memset(schoolCache, 0, sizeof(*schoolCache));
    schoolCache->generation = generation + 1;
  }
  if (schoolBuffer != nullptr) {
    heap_caps_free(schoolBuffer);
    schoolBuffer = nullptr;
  }
  schoolLastSuccessAt = 0;
}

SchoolCache *ensureCache() {
  if (schoolCache == nullptr) {
    schoolCache = static_cast<SchoolCache *>(heap_caps_calloc(
        1, sizeof(SchoolCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return schoolCache;
}

uint8_t *ensureBuffer() {
  if (schoolBuffer == nullptr) {
    schoolBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        SCHOOL_MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return schoolBuffer;
}

// Tělo odpovědi do bufferu v PSRAM. Nad strop se přestane přijímat a nahlásí
// se přetečení, aby se nerozebíral useknutý dokument.
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

// Volá se pod zámkem. Zahodí kopii, na které by DNES a ZÍTRA už neseděly.
bool expireStaleLocked(SchoolCache &cache) {
  if (!cache.ready || schoolLastSuccessAt == 0 ||
      millis() - schoolLastSuccessAt <= SCHOOL_STALE_MS) {
    return false;
  }
  cache.ready = false;
  if (cache.message[0] == '\0') {
    strlcpy(cache.message, "Rozvrh nyní není dostupný.", sizeof(cache.message));
  }
  ++cache.generation;
  return true;
}

bool takeLock(TickType_t wait) {
  return schoolMutex != nullptr && xSemaphoreTake(schoolMutex, wait) == pdTRUE;
}

}  // namespace

void schoolServiceBegin() {
  if (schoolMutex == nullptr) {
    schoolMutex = xSemaphoreCreateMutexStatic(&schoolMutexStorage);
  }
}

bool schoolServiceStatus(SchoolStatus &status) {
  status = SchoolStatus{};
  if (!takeLock(pdMS_TO_TICKS(20))) return false;
  status.loading = schoolLoading;
  if (schoolLastSuccessAt != 0) {
    status.lastSuccessAvailable = true;
    status.lastSuccessAgeMs =
        static_cast<uint32_t>(millis() - schoolLastSuccessAt);
  }
  if (schoolCache != nullptr) {
    status.generation = schoolCache->generation;
    status.ready = schoolCache->ready;
    for (size_t index = 0; index < schoolCache->feed.dayCount; ++index)
      status.lessonCount += schoolCache->feed.days[index].lessonCount;
    status.homeworkCount = schoolCache->feed.homeworkCount;
    strlcpy(status.message, schoolCache->message, sizeof(status.message));
  }
  xSemaphoreGive(schoolMutex);
  return true;
}

bool schoolServiceVisit(SchoolFeedVisitor visitor, void *context) {
  if (visitor == nullptr || !takeLock(pdMS_TO_TICKS(20))) return false;
  const bool ready = schoolCache != nullptr && schoolCache->ready;
  if (ready) visitor(schoolCache->feed, context);
  xSemaphoreGive(schoolMutex);
  return ready;
}

bool schoolServiceVisitProbe(SchoolFeedVisitor visitor, void *context) {
  if (visitor == nullptr || !takeLock(pdMS_TO_TICKS(20))) return false;
  const bool ready = schoolCache != nullptr && schoolCache->probeReady;
  if (ready) visitor(schoolCache->probe, context);
  xSemaphoreGive(schoolMutex);
  return ready;
}

// Společné tělo běžného stažení i zkoušky z webu; liší se jen tím, kam se
// rozebraný rozvrh uloží.
static bool schoolServiceDownload(const ClockSchoolConfig &config,
                                  NetworkDiagnosticKind diagnosticKind,
                                  bool probeOnly, int &httpStatus,
                                  String &error) {
  httpStatus = HTTPC_ERROR_CONNECTION_REFUSED;
  error = "";
  if (config.url[0] == '\0') {
    error = F("Adresa rozvrhu není vyplněná.");
    return false;
  }
  const bool secure = strncmp(config.url, "https://", 8) == 0;
  if (!secure && strncmp(config.url, "http://", 7) != 0) {
    error = F("Adresa musí začínat http:// nebo https://.");
    return false;
  }
  // Web takovou adresu neuloží, ale mohla přijít ze zálohy nastavení. Chyba
  // jde stejnou cestou jako neúspěšné stažení, aby ji obrazovka ukázala.
  const bool credentialsInClear =
      !secure && clockConfigUrlHasCredentials(config.url);

  networkDiagnosticsBegin(diagnosticKind);
  NetworkOperationGuard networkGuard(SCHOOL_NETWORK_GUARD_MS);
  if (!networkGuard) {
    error = F("Síť je právě vytížená jinou operací.");
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }

  if (!takeLock(portMAX_DELAY)) {
    error = F("Rozvrh nyní není dostupný.");
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  schoolLoading = true;
  schoolFetchActive = true;
  SchoolCache *cache = ensureCache();
  uint8_t *buffer = ensureBuffer();
  if (cache == nullptr || buffer == nullptr) {
    schoolLoading = false;
    schoolFetchActive = false;
    if (schoolClearPending) {
      schoolClearPending = false;
      releaseStorage();
    }
    error = F("Pro rozvrh není dostatek PSRAM.");
    xSemaphoreGive(schoolMutex);
    networkDiagnosticsSetDetail(diagnosticKind, error);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  xSemaphoreGive(schoolMutex);

  size_t payloadLength = 0;
  if (credentialsInClear) {
    error = F("Adresa rozvrhu s heslem musí začínat https://.");
  } else {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (secure) {
      secureClient.setCACertBundle(
          rootca_crt_bundle_start,
          static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
      secureClient.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    }
    HTTPClient http;
    http.setConnectTimeout(SCHOOL_CONNECT_TIMEOUT_MS);
    http.setTimeout(SCHOOL_RESPONSE_TIMEOUT_MS);
    // Bez setReuse(false) by si HTTPClient spojení schoval a TLS kontexty by
    // zůstaly alokované natrvalo - viz RssService.cpp a AgendaService.cpp.
    http.setReuse(false);
    // Adresa nese heslo pro basic_auth; přesměrování by ho mohlo odnést jinam.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent(F("WaveshareHodiny"));
    WiFiClient &client = secure ? static_cast<WiFiClient &>(secureClient)
                                : plainClient;
    httpDownloadPrepare(http);
    if (http.begin(client, config.url)) {
      httpStatus = http.GET();
      if (httpStatus == HTTP_CODE_OK) {
        const int declaredSize = http.getSize();
        if (declaredSize > static_cast<int>(SCHOOL_MAX_RESPONSE_BYTES)) {
          error = F("Odpověď serveru je příliš velká.");
        } else {
          BoundedBufferStream response(buffer, SCHOOL_MAX_RESPONSE_BYTES);
          // Ne writeToStream(): u chunked odpovědi se nemusí vrátit.
          const int bytesRead =
              httpDownloadBody(http, response, SCHOOL_RESPONSE_TIMEOUT_MS);
          payloadLength = response.length();
          if (response.overflowed()) {
            error = F("Odpověď serveru je příliš velká.");
          } else if (bytesRead < 0) {
            // Useknuté tělo by se rozebralo jako kratší rozvrh.
            error = F("Rozvrh nyní není dostupný.");
          }
        }
      }
      http.end();
    }
    client.stop();
  }
  // Core 3.0.7 svazek kořenů po spojení neodpojí a každé stažení by ukouslo
  // kus interní RAM. Odpojuje se až po zániku klienta.
  if (secure) esp_crt_bundle_detach(nullptr);

  if (error.isEmpty() && httpStatus != HTTP_CODE_OK) {
    if (httpStatus == HTTP_CODE_NOT_FOUND) {
      error = F("Na zadané adrese rozvrh není.");
    } else if (httpStatus == HTTP_CODE_UNAUTHORIZED) {
      error = F("Server odmítl heslo v adrese rozvrhu.");
    } else if (httpStatus == HTTP_CODE_SERVICE_UNAVAILABLE) {
      // 503 posílá serve.py, dokud se poprvé nepřihlásil do Školy OnLine,
      // nebo když jsou jeho data starší než SCHOOL_MAX_AGE_HOURS.
      error = F("Server nemá čerstvý rozvrh ze Školy OnLine.");
    } else {
      error = F("Rozvrh nyní není dostupný.");
    }
  }

  SchoolFeed &parsed = cache->scratch;
  if (error.isEmpty()) {
    const SchoolParseStatus status = schoolParseFeed(
        reinterpret_cast<const char *>(buffer), payloadLength, parsed);
    if (status == SchoolParseStatus::NotJson) {
      error = F("Odpověď serveru není JSON.");
    } else if (status == SchoolParseStatus::MissingArray) {
      error = F("Odpověď serveru nemá rozvrh.");
    }
  }

  if (!takeLock(portMAX_DELAY)) {
    networkDiagnosticsSetDetail(diagnosticKind, F("Zámek rozvrhu selhal."));
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  schoolLoading = false;
  schoolFetchActive = false;
  if (schoolClearPending) {
    // Obrazovka se během stahování vypnula nebo změnila adresu.
    schoolClearPending = false;
    releaseStorage();
    xSemaphoreGive(schoolMutex);
    networkDiagnosticsEnd(diagnosticKind, false, httpStatus);
    return false;
  }
  const bool ok = error.isEmpty();
  if (probeOnly) {
    cache->probeReady = ok;
    if (ok) cache->probe = parsed;
    networkDiagnosticsSetDetail(diagnosticKind,
                                ok ? String(F("Zkouška načetla rozvrh."))
                                   : error);
  } else if (ok) {
    // Prázdný rozvrh je platný výsledek (prázdniny), ne chyba.
    cache->feed = parsed;
    cache->ready = true;
    cache->message[0] = '\0';
    ++cache->generation;
    schoolLastSuccessAt = millis();
    size_t lessons = 0;
    for (size_t index = 0; index < parsed.dayCount; ++index)
      lessons += parsed.days[index].lessonCount;
    String detail = F("Načteno dnů: ");
    detail += parsed.dayCount;
    detail += F(", hodin: ");
    detail += lessons;
    detail += F(", úkolů: ");
    detail += parsed.homeworkCount;
    if (parsed.hasMessages) {
      detail += F(", zpráv: ");
      detail += parsed.messageTotal;
    }
    if (parsed.hasMarks) {
      detail += F(", známek: ");
      detail += parsed.markTotal;
    }
    networkDiagnosticsSetDetail(diagnosticKind, detail);
  } else {
    // Poslední úspěšný rozvrh zůstává a hláška se ukáže, jen když žádný není.
    // Zahodí se, když server sám řekne, že platná data nemá (503), nebo když
    // je kopie tak stará, že by DNES a ZÍTRA už neseděly.
    strlcpy(cache->message, error.c_str(), sizeof(cache->message));
    if (!cache->ready) {
      ++cache->generation;
    } else if (httpStatus == HTTP_CODE_SERVICE_UNAVAILABLE) {
      cache->ready = false;
      ++cache->generation;
    } else {
      expireStaleLocked(*cache);
    }
    networkDiagnosticsSetDetail(diagnosticKind, error);
  }
  xSemaphoreGive(schoolMutex);
  networkDiagnosticsEnd(diagnosticKind, ok, httpStatus);
  return ok;
}

bool schoolServiceFetch(const ClockSchoolConfig &config,
                        NetworkDiagnosticKind diagnosticKind, int &httpStatus,
                        String &error) {
  return schoolServiceDownload(config, diagnosticKind, false, httpStatus,
                               error);
}

bool schoolServiceProbe(const ClockSchoolConfig &config, int &httpStatus,
                        String &error) {
  return schoolServiceDownload(config, NetworkDiagnosticKind::SchoolTest, true,
                               httpStatus, error);
}

void schoolServiceExpireStale() {
  if (!takeLock(pdMS_TO_TICKS(20))) return;
  if (schoolCache != nullptr) expireStaleLocked(*schoolCache);
  xSemaphoreGive(schoolMutex);
}

void schoolServiceClear() {
  if (!takeLock(portMAX_DELAY)) {
    schoolClearPending = true;
    return;
  }
  if (schoolFetchActive) {
    schoolClearPending = true;
    xSemaphoreGive(schoolMutex);
    return;
  }
  schoolClearPending = false;
  releaseStorage();
  schoolLoading = false;
  xSemaphoreGive(schoolMutex);
}
