#include "SettingsShareService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "HttpDownload.h"
#include "NetworkCoordinator.h"

// Kořenové certifikáty Mozilly slinkované v mbedTLS. Server zadává uživatel,
// takže připnout jeden kořen nejde - stejně jako u agendy a zpráv.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
constexpr uint32_t SHARE_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t SHARE_RESPONSE_TIMEOUT_MS = 8000;
constexpr uint32_t SHARE_NETWORK_GUARD_MS = 10000;

char *responseBuffer = nullptr;

class BufferPrint : public Print {
 public:
  BufferPrint(char *buffer, size_t capacity)
      : buffer_(buffer), capacity_(capacity) {}

  size_t write(uint8_t value) override { return write(&value, 1); }

  size_t write(const uint8_t *data, size_t size) override {
    const size_t remaining = capacity_ - length_;
    const size_t accepted = size < remaining ? size : remaining;
    if (accepted > 0) memcpy(buffer_ + length_, data, accepted);
    length_ += accepted;
    if (accepted != size) overflowed_ = true;
    return accepted;
  }

  size_t length() const { return length_; }
  bool overflowed() const { return overflowed_; }

 private:
  char *buffer_;
  size_t capacity_;
  size_t length_ = 0;
  bool overflowed_ = false;
};

void setError(SettingsShareResult &result, const String &message) {
  strlcpy(result.error, message.c_str(), sizeof(result.error));
}

String operationUrl(const SettingsShareRequest &request) {
  String url = request.url;
  while (url.endsWith("/")) url.remove(url.length() - 1);
  url += '/';
  if (request.operation != SettingsShareOperation::List) url += request.name;
  return url;
}

bool operationSucceeded(SettingsShareOperation operation, int status) {
  switch (operation) {
    case SettingsShareOperation::Upload:
      return status == HTTP_CODE_OK || status == HTTP_CODE_CREATED;
    case SettingsShareOperation::Delete:
      return status == HTTP_CODE_OK || status == HTTP_CODE_NO_CONTENT;
    default:
      return status == HTTP_CODE_OK;
  }
}

void describeFailure(const SettingsShareRequest &request,
                     SettingsShareResult &result) {
  const int status = result.httpStatus;
  if (status == HTTP_CODE_UNAUTHORIZED || status == HTTP_CODE_FORBIDDEN) {
    setError(result, F("Server odmítl jméno nebo heslo v adrese."));
  } else if (status == HTTP_CODE_NOT_FOUND) {
    setError(result, request.operation == SettingsShareOperation::Download ||
                             request.operation == SettingsShareOperation::Delete
                         ? F("Záloha s tímto názvem na serveru není.")
                         : F("Na zadané adrese server pro zálohy není."));
  } else if (status == HTTP_CODE_PAYLOAD_TOO_LARGE) {
    setError(result, F("Server zálohu odmítl jako příliš velkou."));
  } else if (status == HTTP_CODE_INSUFFICIENT_STORAGE) {
    setError(result, F("Na serveru je už příliš mnoho záloh."));
  } else if (status < 0) {
    setError(result, F("Server se nepodařilo kontaktovat."));
  } else {
    String message = F("Server odpověděl chybou HTTP ");
    message += status;
    message += '.';
    setError(result, message);
  }
}
}  // namespace

void settingsShareExecute(const SettingsShareRequest &request,
                          SettingsShareResult &result) {
  result = SettingsShareResult{};
  if (!settingsShareValidUrl(request.url)) {
    setError(result, F("Adresa serveru musí začínat https://."));
    return;
  }
  if (request.operation != SettingsShareOperation::List &&
      !settingsBackupValidName(request.name)) {
    setError(result, F("Název zálohy není platný."));
    return;
  }
  if (request.operation == SettingsShareOperation::Upload &&
      (request.body == nullptr || request.bodyLength == 0)) {
    setError(result, F("Záloha je prázdná."));
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setError(result, F("Hodiny nejsou připojené k Wi-Fi."));
    return;
  }
  if (responseBuffer == nullptr) {
    responseBuffer = static_cast<char *>(heap_caps_malloc(
        SETTINGS_SHARE_MAX_RESPONSE_BYTES + 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (responseBuffer == nullptr) {
    setError(result, F("Pro zálohu není dostatek PSRAM."));
    return;
  }

  NetworkOperationGuard networkGuard(SHARE_NETWORK_GUARD_MS);
  if (!networkGuard) {
    setError(result, F("Síť je právě vytížená jinou operací."));
    return;
  }

  size_t responseLength = 0;
  bool bodyFailed = false;
  {
    WiFiClientSecure client;
    client.setCACertBundle(
        rootca_crt_bundle_start,
        static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
    client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    HTTPClient http;
    http.setConnectTimeout(SHARE_CONNECT_TIMEOUT_MS);
    http.setTimeout(SHARE_RESPONSE_TIMEOUT_MS);
    // Viz AgendaService.cpp: bez toho by si HTTPClient spojení schoval a TLS
    // buffery by zůstaly alokované navždy.
    http.setReuse(false);
    // Přesměrování se nenásleduje: heslo z adresy by šlo i na cizí server.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent(F("WaveshareHodiny"));
    httpDownloadPrepare(http);
    if (http.begin(client, operationUrl(request))) {
      if (request.operation == SettingsShareOperation::Upload) {
        http.addHeader(F("Content-Type"), F("application/json"));
        result.httpStatus = http.sendRequest(
            "PUT", reinterpret_cast<uint8_t *>(const_cast<char *>(request.body)),
            request.bodyLength);
      } else if (request.operation == SettingsShareOperation::Delete) {
        result.httpStatus = http.sendRequest("DELETE");
      } else {
        http.addHeader(F("Accept"), F("application/json"));
        result.httpStatus = http.GET();
      }
      // Upload a Delete tělo odpovědi nepotřebují; stačí stav.
      const bool readsBody =
          request.operation == SettingsShareOperation::List ||
          request.operation == SettingsShareOperation::Download;
      if (readsBody && result.httpStatus == HTTP_CODE_OK) {
        BufferPrint response(responseBuffer, SETTINGS_SHARE_MAX_RESPONSE_BYTES);
        const int bytesRead =
            httpDownloadBody(http, response, SHARE_RESPONSE_TIMEOUT_MS);
        responseLength = response.length();
        if (response.overflowed()) {
          setError(result, F("Odpověď serveru je příliš velká."));
          bodyFailed = true;
        } else if (bytesRead < 0) {
          setError(result, F("Odpověď serveru přišla neúplná."));
          bodyFailed = true;
        }
      }
      http.end();
    }
    client.stop();
  }
  // Arduino core 3.0.7 svazek kořenů při ukončení spojení neodpojí; bez toho
  // by každý přenos ukousl kus interní RAM. Viz AgendaService.cpp.
  esp_crt_bundle_detach(nullptr);

  if (bodyFailed) return;
  if (!operationSucceeded(request.operation, result.httpStatus)) {
    describeFailure(request, result);
    return;
  }
  responseBuffer[responseLength] = '\0';
  result.body = responseBuffer;
  result.bodyLength = responseLength;
  result.ok = true;
}
