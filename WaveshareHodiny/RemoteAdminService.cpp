#include "RemoteAdminService.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include <cctype>
#include <cstring>

#include "ConfigurationWeb.h"
#include "DeviceName.h"
#include "FirmwareBuild.h"
#include "NetworkCoordinator.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {
constexpr char PREFS_NAMESPACE[] = "remote-admin";
constexpr char PREFS_ENABLED[] = "on";
constexpr char PREFS_URL[] = "url";
constexpr char PREFS_TOKEN[] = "token";

// Server drží dotaz 25 s; rezerva na pomalou síť.
constexpr uint32_t POLL_TIMEOUT_MS = 40000;
constexpr uint32_t RESULT_TIMEOUT_MS = 15000;
constexpr uint32_t BODY_TIMEOUT_MS = 20000;
// Nejpomalejší obsluhy webu (zkouška kanálu, šifrování zálohy) trvají
// jednotky až desítky sekund. Server čeká 75 s.
constexpr uint32_t LOCAL_TIMEOUT_MS = 65000;
constexpr uint32_t LOCAL_CONNECT_TIMEOUT_MS = 3000;
constexpr size_t HEAD_CAPACITY = 1536;
constexpr size_t MAX_REQUEST_BODY = 64 * 1024;
// Stránka nastavení má gzipem kolem 80 kB, překlady 40 kB.
constexpr size_t MAX_RESPONSE = 1024 * 1024;
constexpr size_t KEEP_BUFFER = 256 * 1024;
constexpr size_t WRITE_CHUNK = 4096;
constexpr uint32_t FIRST_RETRY_MS = 5000;
constexpr uint32_t MAX_RETRY_MS = 5 * 60 * 1000;
constexpr uint32_t REJECTED_RETRY_MS = 10 * 60 * 1000;

struct Settings {
  bool enabled = false;
  char url[REMOTE_ADMIN_URL_LENGTH] = "";
  char token[REMOTE_ADMIN_TOKEN_LENGTH + 1] = "";
};

TaskHandle_t taskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
Settings settings;
uint32_t settingsRevision = 0;
bool loaded = false;
bool suspended = false;
bool connected = false;
uint32_t lastContactMs = 0;
bool contacted = false;
uint32_t requestsServed = 0;
char statusMessage[80] = "Vypnuto";
// Úloha má zásobník v PSRAM, a ta je při čtení flash vypnutá: NVS (a tedy
// deviceNameLoad) smí sahat jen volající s vnitřním zásobníkem. Jméno se
// proto načte při startu a úloha si bere kopii.
char deviceName[DEVICE_NAME_LENGTH] = "";

void setStatus(const char *message, bool isConnected) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(statusMessage, message, sizeof(statusMessage));
  connected = isConnected;
  portEXIT_CRITICAL(&stateMux);
}

void markContact() {
  portENTER_CRITICAL(&stateMux);
  lastContactMs = millis();
  contacted = true;
  portEXIT_CRITICAL(&stateMux);
}

void loadSettings() {
  Settings next;
  Preferences preferences;
  if (preferences.begin(PREFS_NAMESPACE, true, "clockcfg")) {
    next.enabled = preferences.getUChar(PREFS_ENABLED, 0) != 0;
    preferences.getString(PREFS_URL, next.url, sizeof(next.url));
    preferences.getString(PREFS_TOKEN, next.token, sizeof(next.token));
    preferences.end();
  }
  RemoteAdminEndpoint endpoint;
  if (!remoteAdminParseUrl(next.url, endpoint) ||
      !remoteAdminValidToken(next.token))
    next.enabled = false;
  portENTER_CRITICAL(&stateMux);
  settings = next;
  ++settingsRevision;
  loaded = true;
  portEXIT_CRITICAL(&stateMux);
}

// --- čtení a zápis spojení ---------------------------------------------

bool writeAll(Client &client, const uint8_t *data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    const size_t chunk =
        length - sent < WRITE_CHUNK ? length - sent : WRITE_CHUNK;
    const size_t written = client.write(data + sent, chunk);
    if (written == 0) return false;
    sent += written;
  }
  return true;
}

bool writeText(Client &client, const char *text) {
  return writeAll(client, reinterpret_cast<const uint8_t *>(text), strlen(text));
}

// Hlavičky čte po bajtech, aby nesáhla do těla; vrací délku bez \r\n\r\n.
bool readHead(Client &client, char *head, size_t capacity, size_t &length,
              uint32_t timeoutMs) {
  length = 0;
  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    const int value = client.read();
    if (value < 0) {
      if (!client.connected()) return false;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (length + 1 >= capacity) return false;
    head[length++] = static_cast<char>(value);
    if (length >= 4 && memcmp(head + length - 4, "\r\n\r\n", 4) == 0) {
      length -= 4;
      head[length] = '\0';
      return true;
    }
  }
  return false;
}

bool readExact(Client &client, uint8_t *buffer, size_t length,
               uint32_t timeoutMs) {
  size_t received = 0;
  uint32_t lastProgress = millis();
  while (received < length) {
    const int got = client.read(buffer + received, length - received);
    if (got > 0) {
      received += static_cast<size_t>(got);
      lastProgress = millis();
      continue;
    }
    if (!client.connected() && client.available() <= 0) return false;
    if (millis() - lastProgress > timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return true;
}

bool discardBody(Client &client, const RemoteAdminHead &head) {
  if (head.chunked) return false;
  long remaining = head.contentLength < 0 ? 0 : head.contentLength;
  if (remaining > 16 * 1024) return false;
  uint8_t scratch[256];
  while (remaining > 0) {
    const size_t chunk = remaining < static_cast<long>(sizeof(scratch))
                             ? static_cast<size_t>(remaining)
                             : sizeof(scratch);
    if (!readExact(client, scratch, chunk, BODY_TIMEOUT_MS)) return false;
    remaining -= static_cast<long>(chunk);
  }
  return true;
}

// --- vyřízení požadavku na vlastním webu ---------------------------------

struct Buffer {
  uint8_t *data = nullptr;
  size_t capacity = 0;
  size_t length = 0;

  bool reserve(size_t wanted) {
    if (wanted <= capacity) return true;
    size_t next = capacity == 0 ? 32 * 1024 : capacity;
    while (next < wanted) next *= 2;
    if (next > MAX_RESPONSE + HEAD_CAPACITY) next = MAX_RESPONSE + HEAD_CAPACITY;
    if (next < wanted) return false;
    void *grown = heap_caps_realloc(data, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (grown == nullptr) return false;
    data = static_cast<uint8_t *>(grown);
    capacity = next;
    return true;
  }

  void trim() {
    if (capacity <= KEEP_BUFFER) return;
    heap_caps_free(data);
    data = nullptr;
    capacity = 0;
    length = 0;
  }
};

struct LocalResult {
  int status = 502;
  char contentType[REMOTE_ADMIN_CONTENT_TYPE_LENGTH] = "application/json";
  bool gzip = false;
  const uint8_t *body = nullptr;
  size_t bodyLength = 0;
};

void failLocal(LocalResult &result, int status, const char *message) {
  static char text[128];
  snprintf(text, sizeof(text), "{\"ok\":false,\"message\":\"%s\"}", message);
  result = LocalResult{};
  result.status = status;
  result.body = reinterpret_cast<const uint8_t *>(text);
  result.bodyLength = strlen(text);
}

void executeLocally(const RemoteAdminHead &job, const uint8_t *body,
                    size_t bodyLength, Buffer &response, LocalResult &result) {
  if (!remoteAdminAllowedRequest(job.jobMethod, job.jobPath)) {
    failLocal(result, 403, "Tohle jde nastavit jen v domácí síti přímo na hodinách.");
    return;
  }
  char head[HEAD_CAPACITY];
  const size_t headLength = remoteAdminBuildLocalRequest(
      head, sizeof(head), job.jobMethod, job.jobPath, job.contentType,
      configurationWebLoopbackKey(), bodyLength);
  if (headLength == 0) {
    failLocal(result, 400, "Požadavek je příliš dlouhý.");
    return;
  }
  WiFiClient local;
  if (!local.connect(IPAddress(127, 0, 0, 1), 80, LOCAL_CONNECT_TIMEOUT_MS)) {
    failLocal(result, 502, "Web hodin neodpovídá.");
    return;
  }
  if (!writeAll(local, reinterpret_cast<const uint8_t *>(head), headLength) ||
      (bodyLength > 0 && !writeAll(local, body, bodyLength))) {
    local.stop();
    failLocal(result, 502, "Web hodin neodpovídá.");
    return;
  }
  // Web hodin sice posílá Connection: close, ale spojení zavře až po klientovi
  // (u prohlížeče hned, tady by se čekalo LOCAL_TIMEOUT_MS). Čte se proto jen
  // do konce odpovědi podle Content-Length nebo posledního bloku chunked.
  response.length = 0;
  const uint32_t started = millis();
  bool overflow = false;
  while (millis() - started < LOCAL_TIMEOUT_MS) {
    if (!response.reserve(response.length + 4096)) {
      overflow = true;
      break;
    }
    const int got = local.read(response.data + response.length,
                               response.capacity - response.length);
    if (got > 0) {
      response.length += static_cast<size_t>(got);
      const long complete =
          remoteAdminCompleteLength(response.data, response.length);
      if (complete > 0) {
        response.length = static_cast<size_t>(complete);
        break;
      }
      continue;
    }
    if (!local.connected() && local.available() <= 0) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  local.stop();
  if (overflow) {
    failLocal(result, 502, "Odpověď hodin je příliš velká.");
    return;
  }
  const size_t headEnd = remoteAdminHeadLength(response.data, response.length);
  RemoteAdminHead parsed;
  if (headEnd == 0 ||
      !remoteAdminParseHead(reinterpret_cast<const char *>(response.data),
                            headEnd - 4, parsed)) {
    failLocal(result, 502, "Web hodin neodpověděl včas.");
    return;
  }
  uint8_t *payload = response.data + headEnd;
  size_t payloadLength = response.length - headEnd;
  if (parsed.chunked) {
    const long plain = remoteAdminDechunk(payload, payloadLength);
    if (plain < 0) {
      failLocal(result, 502, "Odpověď hodin přišla neúplná.");
      return;
    }
    payloadLength = static_cast<size_t>(plain);
  } else if (parsed.contentLength >= 0) {
    if (static_cast<size_t>(parsed.contentLength) > payloadLength) {
      failLocal(result, 502, "Odpověď hodin přišla neúplná.");
      return;
    }
    payloadLength = static_cast<size_t>(parsed.contentLength);
  }
  result = LocalResult{};
  result.status = parsed.status;
  strlcpy(result.contentType,
          parsed.contentType[0] != '\0' ? parsed.contentType
                                        : "application/octet-stream",
          sizeof(result.contentType));
  result.gzip = parsed.gzip;
  result.body = payload;
  result.bodyLength = payloadLength;
}

// --- spojení se serverem --------------------------------------------------

enum class SessionEnd : uint8_t { Reconnect, Rejected, Failed, Stopped };

struct Connection {
  RemoteAdminEndpoint endpoint;
  char hostHeader[REMOTE_ADMIN_HOST_LENGTH + 8] = "";
  char token[REMOTE_ADMIN_TOKEN_LENGTH + 1] = "";
  char deviceName[DEVICE_NAME_LENGTH] = "";
};

bool stillWanted(uint32_t revision) {
  portENTER_CRITICAL(&stateMux);
  const bool wanted = !suspended && settings.enabled && settingsRevision == revision;
  portEXIT_CRITICAL(&stateMux);
  return wanted && WiFi.status() == WL_CONNECTED;
}

SessionEnd runSession(WiFiClientSecure &client, const Connection &connection,
                      uint32_t revision, uint8_t *requestBody, Buffer &response) {
  char head[HEAD_CAPACITY];
  size_t headLength = 0;
  while (stillWanted(revision)) {
    snprintf(head, sizeof(head),
             "GET %s/agent/poll HTTP/1.1\r\nHost: %s\r\n"
             "Authorization: Bearer %s\r\nUser-Agent: WaveshareHodiny\r\n"
             "X-Clock-Firmware: %s\r\nX-Clock-Name: %s\r\n\r\n",
             connection.endpoint.basePath, connection.hostHeader,
             connection.token, FIRMWARE_VERSION, connection.deviceName);
    const uint32_t pollSentMs = millis();
    if (!writeText(client, head)) return SessionEnd::Failed;
    RemoteAdminHead job;
    if (!readHead(client, head, sizeof(head), headLength, POLL_TIMEOUT_MS) ||
        !remoteAdminParseHead(head, headLength, job))
      return SessionEnd::Failed;
    markContact();
    if (job.status == 401 || job.status == 403) return SessionEnd::Rejected;
    if (job.status == 204) {
      setStatus("Připojeno k serveru", true);
      if (!discardBody(client, job)) return SessionEnd::Failed;
      if (job.close) return SessionEnd::Reconnect;
      continue;
    }
    if (job.status != 200 || job.chunked || job.contentLength < 0 ||
        static_cast<size_t>(job.contentLength) > MAX_REQUEST_BODY ||
        job.jobId[0] == '\0')
      return SessionEnd::Failed;
    // Id úlohy je od serveru a vrací se v hlavičce; smí mít jen bezpečné znaky.
    for (const char *c = job.jobId; *c != '\0'; ++c)
      if (!isalnum(static_cast<unsigned char>(*c))) return SessionEnd::Failed;
    const uint32_t jobHeadMs = millis();
    const size_t bodyLength = static_cast<size_t>(job.contentLength);
    if (bodyLength > 0 &&
        !readExact(client, requestBody, bodyLength, BODY_TIMEOUT_MS))
      return SessionEnd::Failed;
    setStatus("Připojeno k serveru", true);

    LocalResult result;
    const uint32_t localStartMs = millis();
    executeLocally(job, requestBody, bodyLength, response, result);
    const uint32_t localDoneMs = millis();
    portENTER_CRITICAL(&stateMux);
    ++requestsServed;
    portEXIT_CRITICAL(&stateMux);

    snprintf(head, sizeof(head),
             "POST %s/agent/result HTTP/1.1\r\nHost: %s\r\n"
             "Authorization: Bearer %s\r\nUser-Agent: WaveshareHodiny\r\n"
             "X-Job-Id: %s\r\nX-Job-Status: %d\r\nX-Job-Content-Type: %s\r\n"
             "X-Job-Content-Encoding: %s\r\nContent-Type: application/octet-stream\r\n"
             "Content-Length: %u\r\n\r\n",
             connection.endpoint.basePath, connection.hostHeader,
             connection.token, job.jobId, result.status, result.contentType,
             result.gzip ? "gzip" : "", static_cast<unsigned>(result.bodyLength));
    const bool sent = writeText(client, head) &&
                      (result.bodyLength == 0 ||
                       writeAll(client, result.body, result.bodyLength));
    const uint32_t resultSentMs = millis();
    response.trim();
    if (!sent) return SessionEnd::Failed;
    RemoteAdminHead ack;
    if (!readHead(client, head, sizeof(head), headLength, RESULT_TIMEOUT_MS) ||
        !remoteAdminParseHead(head, headLength, ack))
      return SessionEnd::Failed;
#if !FIRMWARE_RELEASE
    // Kde se ztrácí čas u požadavků přes server (vývojové buildy).
    Serial.printf("[relay] %s %s %d %u B | poll->job %lu ms, body %lu, local %lu, "
                  "upload %lu, ack %lu\n",
                  job.jobMethod, job.jobPath, result.status,
                  static_cast<unsigned>(result.bodyLength),
                  static_cast<unsigned long>(jobHeadMs - pollSentMs),
                  static_cast<unsigned long>(localStartMs - jobHeadMs),
                  static_cast<unsigned long>(localDoneMs - localStartMs),
                  static_cast<unsigned long>(resultSentMs - localDoneMs),
                  static_cast<unsigned long>(millis() - resultSentMs));
#endif
    if (ack.status == 401 || ack.status == 403) return SessionEnd::Rejected;
    if (!discardBody(client, ack)) return SessionEnd::Failed;
    if (ack.close) return SessionEnd::Reconnect;
  }
  return SessionEnd::Stopped;
}

void remoteAdminTask(void *) {
  // Tělo požadavku od serveru; odpověď webu roste podle potřeby.
  uint8_t *requestBody = static_cast<uint8_t *>(
      heap_caps_malloc(MAX_REQUEST_BODY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  Buffer response;
  uint32_t failures = 0;
  uint32_t retryAt = 0;
  uint32_t lastRevision = UINT32_MAX;

  for (;;) {
    Settings current;
    portENTER_CRITICAL(&stateMux);
    current = settings;
    const uint32_t revision = settingsRevision;
    const bool stopped = suspended;
    portEXIT_CRITICAL(&stateMux);

    if (revision != lastRevision) {
      lastRevision = revision;
      failures = 0;
      retryAt = 0;
    }
    if (stopped || !current.enabled) {
      setStatus(stopped ? "Pozastaveno kvůli aktualizaci" : "Vypnuto", false);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }
    if (requestBody == nullptr) {
      setStatus("Nedostatek paměti", false);
      requestBody = static_cast<uint8_t *>(heap_caps_malloc(
          MAX_REQUEST_BODY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      setStatus("Čeká na Wi-Fi", false);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }
    if (retryAt != 0 && static_cast<long>(millis() - retryAt) < 0) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }

    Connection connection;
    if (!remoteAdminParseUrl(current.url, connection.endpoint)) {
      setStatus("Adresa serveru není platná", false);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }
    if (connection.endpoint.port == 443)
      strlcpy(connection.hostHeader, connection.endpoint.host,
              sizeof(connection.hostHeader));
    else
      snprintf(connection.hostHeader, sizeof(connection.hostHeader), "%s:%u",
               connection.endpoint.host,
               static_cast<unsigned>(connection.endpoint.port));
    strlcpy(connection.token, current.token, sizeof(connection.token));
    portENTER_CRITICAL(&stateMux);
    strlcpy(connection.deviceName, deviceName, sizeof(connection.deviceName));
    portEXIT_CRITICAL(&stateMux);

    setStatus("Připojuji k serveru…", false);
    SessionEnd end = SessionEnd::Failed;
    {
      WiFiClientSecure client;
      client.setCACertBundle(
          rootca_crt_bundle_start,
          static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start));
      client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
      if (client.connect(connection.endpoint.host, connection.endpoint.port)) {
        end = runSession(client, connection, revision, requestBody, response);
      }
      client.stop();
    }
    // Core připojuje svazek kořenů při každém spojení, ale nikdy ho neodpojí.
    esp_crt_bundle_detach(nullptr);
    response.trim();

    switch (end) {
      case SessionEnd::Stopped:
      case SessionEnd::Reconnect:
        failures = 0;
        retryAt = 0;
        break;
      case SessionEnd::Rejected:
        setStatus("Server token odmítl", false);
        retryAt = millis() + REJECTED_RETRY_MS;
        break;
      case SessionEnd::Failed: {
        portENTER_CRITICAL(&stateMux);
        const bool wasConnected = connected;
        portEXIT_CRITICAL(&stateMux);
        setStatus(wasConnected ? "Spojení se serverem se přerušilo"
                               : "Server není dostupný",
                  false);
        ++failures;
        uint32_t wait = FIRST_RETRY_MS << (failures < 7 ? failures - 1 : 6);
        if (wait > MAX_RETRY_MS) wait = MAX_RETRY_MS;
        retryAt = millis() + wait;
        break;
      }
    }
  }
}
}  // namespace

void remoteAdminServiceBegin() {
  if (!loaded) loadSettings();
  char name[DEVICE_NAME_LENGTH];
  deviceNameLoad(name, sizeof(name));
  remoteAdminServiceSetDeviceName(name);
  if (taskHandle != nullptr) {
    portENTER_CRITICAL(&stateMux);
    suspended = false;
    portEXIT_CRITICAL(&stateMux);
    xTaskNotifyGive(taskHandle);
    return;
  }
  xTaskCreatePinnedToCoreWithCaps(remoteAdminTask, "remote-admin", 16384,
                                  nullptr, 1, &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void remoteAdminServiceSetDeviceName(const char *name) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(deviceName, name, sizeof(deviceName));
  portEXIT_CRITICAL(&stateMux);
}

void remoteAdminServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  suspended = true;
  portEXIT_CRITICAL(&stateMux);
}

void remoteAdminServiceSettings(RemoteAdminSettings &out) {
  if (!loaded) loadSettings();
  portENTER_CRITICAL(&stateMux);
  out.enabled = settings.enabled;
  strlcpy(out.url, settings.url, sizeof(out.url));
  out.tokenSet = settings.token[0] != '\0';
  portEXIT_CRITICAL(&stateMux);
}

RemoteAdminSaveResult remoteAdminServiceSave(bool enabled, const char *url,
                                             const char *token) {
  if (!loaded) loadSettings();
  Settings next;
  portENTER_CRITICAL(&stateMux);
  next = settings;
  portEXIT_CRITICAL(&stateMux);
  RemoteAdminEndpoint endpoint;
  if (url[0] != '\0' && !remoteAdminParseUrl(url, endpoint))
    return RemoteAdminSaveResult::InvalidUrl;
  if (token[0] != '\0' && !remoteAdminValidToken(token))
    return RemoteAdminSaveResult::InvalidToken;
  strlcpy(next.url, url, sizeof(next.url));
  if (token[0] != '\0') strlcpy(next.token, token, sizeof(next.token));
  next.enabled = enabled;
  if (enabled && next.url[0] == '\0') return RemoteAdminSaveResult::InvalidUrl;
  if (enabled && next.token[0] == '\0') return RemoteAdminSaveResult::MissingToken;

  Preferences preferences;
  if (!preferences.begin(PREFS_NAMESPACE, false, "clockcfg"))
    return RemoteAdminSaveResult::StorageFailed;
  bool saved = preferences.putUChar(PREFS_ENABLED, next.enabled ? 1 : 0) == 1;
  if (next.url[0] == '\0') {
    preferences.remove(PREFS_URL);
  } else {
    saved = saved &&
            preferences.putString(PREFS_URL, next.url) == strlen(next.url);
  }
  if (next.token[0] != '\0')
    saved = saved &&
            preferences.putString(PREFS_TOKEN, next.token) == strlen(next.token);
  preferences.end();
  if (!saved) return RemoteAdminSaveResult::StorageFailed;
  portENTER_CRITICAL(&stateMux);
  settings = next;
  ++settingsRevision;
  portEXIT_CRITICAL(&stateMux);
  if (taskHandle != nullptr) xTaskNotifyGive(taskHandle);
  return RemoteAdminSaveResult::Ok;
}

bool remoteAdminServiceForget() {
  Preferences preferences;
  if (!preferences.begin(PREFS_NAMESPACE, false, "clockcfg")) return false;
  const bool cleared = preferences.clear();
  preferences.end();
  if (!cleared) return false;
  portENTER_CRITICAL(&stateMux);
  settings = Settings{};
  ++settingsRevision;
  loaded = true;
  portEXIT_CRITICAL(&stateMux);
  if (taskHandle != nullptr) xTaskNotifyGive(taskHandle);
  return true;
}

void remoteAdminServiceStatus(RemoteAdminStatus &out) {
  portENTER_CRITICAL(&stateMux);
  out.connected = connected;
  out.secondsSinceContact =
      contacted ? (millis() - lastContactMs) / 1000 : UINT32_MAX;
  out.requestsServed = requestsServed;
  strlcpy(out.message, statusMessage, sizeof(out.message));
  portEXIT_CRITICAL(&stateMux);
}
