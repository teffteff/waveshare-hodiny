#include "WeatherHistoryService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <new>

#include "HttpDownload.h"
#include "NetworkCoordinator.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
constexpr char USER_AGENT[] = "WaveshareHodiny";
constexpr uint32_t CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t NETWORK_GUARD_MS = 10000;
// Odpověď má kolem 1,2 kB; strop s rezervou na delší čísla a víc bodů.
constexpr size_t MAX_RESPONSE_BYTES = 6 * 1024;

// Zapisuje tělo odpovědi do bufferu; nad strop se přestane přijímat. Stejná
// třída jako u deště a výstrah - každá služba má svou kopii, aby změna
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

// Buffer i obě kopie historie leží v PSRAM; interní RAM patří TLS.
struct HistoryCache {
  WeatherHistoryData data;
  WeatherHistoryData scratch;
  uint32_t generation = 0;
  bool ready = false;
};

SemaphoreHandle_t historyMutex = nullptr;
HistoryCache *cache = nullptr;
uint8_t *responseBuffer = nullptr;

bool ensureMemory() {
  if (cache == nullptr) {
    void *memory = heap_caps_malloc(sizeof(HistoryCache),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) return false;
    cache = new (memory) HistoryCache();
  }
  if (responseBuffer == nullptr) {
    responseBuffer = static_cast<uint8_t *>(heap_caps_malloc(
        MAX_RESPONSE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (responseBuffer == nullptr) return false;
  }
  return true;
}

long download(const char *url) {
  long result = -1;
  const bool secure = strncmp(url, "https://", 8) == 0;
  {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (secure) {
      // Adresu serveru zadává uživatel, takže se ověřuje proti svazku kořenů
      // Mozilly, stejně jako u srážek a výstrah.
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
      if (http.GET() == HTTP_CODE_OK) {
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
}  // namespace

void weatherHistoryServiceBegin() {
  if (historyMutex == nullptr) historyMutex = xSemaphoreCreateMutex();
}

bool weatherHistoryServiceFetch(const char *url) {
  if (url == nullptr || url[0] == '\0' || historyMutex == nullptr ||
      WiFi.status() != WL_CONNECTED || !ensureMemory())
    return false;
  long length = -1;
  {
    NetworkOperationGuard guard(NETWORK_GUARD_MS);
    if (!guard) return false;
    length = download(url);
  }
  // Rozbor do pracovní kopie mimo zámek; do platné se jen přesune.
  if (length <= 0 ||
      !weatherHistoryParse(reinterpret_cast<const char *>(responseBuffer),
                           static_cast<size_t>(length), cache->scratch))
    return false;
  xSemaphoreTake(historyMutex, portMAX_DELAY);
  cache->data = cache->scratch;
  cache->ready = true;
  ++cache->generation;
  xSemaphoreGive(historyMutex);
  return true;
}

void weatherHistoryServiceClear() {
  if (historyMutex == nullptr || cache == nullptr) return;
  xSemaphoreTake(historyMutex, portMAX_DELAY);
  if (cache->ready) {
    cache->ready = false;
    ++cache->generation;
  }
  xSemaphoreGive(historyMutex);
}

bool weatherHistoryServiceSnapshot(uint32_t &generation,
                                   WeatherHistoryData &data, bool &ready) {
  if (historyMutex == nullptr || cache == nullptr) return false;
  if (xSemaphoreTake(historyMutex, 0) != pdTRUE) return false;
  const bool changed = cache->generation != generation;
  if (changed) {
    generation = cache->generation;
    ready = cache->ready;
    if (ready) data = cache->data;
  }
  xSemaphoreGive(historyMutex);
  return changed;
}
