#include "HomeControlService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <string.h>

#include <new>

#include "ClockConfig.h"
#include "HttpDownload.h"
#include "NetworkCoordinator.h"

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 6000;
constexpr uint32_t NETWORK_GUARD_MS = 8000;
// Viditelná obrazovka se čte každé 3 s; po chybě dvakrát déle, nejvýš 60 s.
constexpr uint32_t VISIBLE_PERIOD_MS = 3000;
constexpr uint32_t MAX_BACKOFF_MS = 60000;
// HA potvrdí přepnutí, až mu ho ohlásí zařízení (tapo-bridge ~0,6 s).
constexpr uint32_t AFTER_COMMAND_READ_MS = 800;
constexpr uint32_t PENDING_TIMEOUT_MS = 6000;

// Vše větší leží v PSRAM; vnitřní RAM má na 7" jen desítky kB.
struct Storage {
  HomeControlTile tiles[HOME_CONTROL_MAX_ITEMS];
  HomeControlItem parsed[HOME_CONTROL_MAX_ITEMS];
  char buffer[HOME_CONTROL_BUFFER_BYTES];
};

Storage *storage = nullptr;
TaskHandle_t taskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

// Pod stateMux:
bool visible = false;
uint32_t requestRevision = 0;
char requestUrl[CLOCK_HA_URL_LENGTH] = "";
char requestToken[CLOCK_HA_TOKEN_LENGTH] = "";
char requestLabel[CLOCK_CONTROLS_LABEL_LENGTH] = "";
uint8_t tileCount = 0;
uint32_t generation = 0;
bool haveData = false;
bool loading = false;
unsigned long lastSuccessAt = 0;
char message[64] = "";
// Jeden čekající příkaz; další stisk se přijme, až se tenhle odešle.
bool commandQueued = false;
char commandEntity[HOME_CONTROL_ENTITY_ID_LENGTH] = "";
HomeControlDomain commandDomain = HomeControlDomain::Switch;
// Kdy byl přepnut stav na displeji; 0 = nic nečeká. Po jedné dlaždici.
unsigned long pendingSince[HOME_CONTROL_MAX_ITEMS] = {};

class BufferSink : public Print {
 public:
  BufferSink(char *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}
  size_t write(uint8_t byte) override {
    if (length_ + 1 >= capacity_) {
      overflowed_ = true;
      return 0;
    }
    buffer_[length_++] = static_cast<char>(byte);
    return 1;
  }
  size_t write(const uint8_t *data, size_t size) override {
    size_t written = 0;
    while (written < size && write(data[written]) == 1) ++written;
    return written;
  }
  size_t length() const { return length_; }
  bool overflowed() const { return overflowed_; }

 private:
  char *buffer_;
  size_t capacity_;
  size_t length_ = 0;
  bool overflowed_ = false;
};

void setMessage(const char *text) {
  portENTER_CRITICAL(&stateMux);
  if (strcmp(message, text) != 0) {
    strlcpy(message, text, sizeof(message));
    ++generation;
  }
  portEXIT_CRITICAL(&stateMux);
}

// Jedno spojení drží úloha, dokud je obrazovka vidět: TLS handshake s HA
// trvá déle než celý dotaz, a stisk tlačítka má odejít hned.
struct Connection {
  WiFiClientSecure secure;
  WiFiClient plain;
  bool secureUrl = false;

  NetworkClient &client() {
    return secureUrl ? static_cast<NetworkClient &>(secure)
                     : static_cast<NetworkClient &>(plain);
  }
  void close() {
    secure.stop();
    plain.stop();
  }
};

// POST na HA. Odpověď (při 200) je v storage->buffer s nulou; vrací kód HTTP
// nebo zápornou chybu HTTPClient. `bodyLength` bajtů těla leží na začátku
// storage->buffer a odpověď ho přepíše.
int post(Connection &connection, const char *url, const char *token,
         size_t bodyLength, size_t &responseLength) {
  responseLength = 0;
  NetworkOperationGuard networkGuard(NETWORK_GUARD_MS);
  if (!networkGuard) return HTTPC_ERROR_CONNECTION_REFUSED;
  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(RESPONSE_TIMEOUT_MS);
  http.setReuse(true);
  httpDownloadPrepare(http);
  if (!http.begin(connection.client(), url)) {
    connection.close();
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  http.addHeader("Authorization", String("Bearer ") + token);
  http.addHeader("Content-Type", "application/json");
  const int status =
      http.POST(reinterpret_cast<uint8_t *>(storage->buffer), bodyLength);
  if (status == HTTP_CODE_OK) {
    BufferSink sink(storage->buffer, sizeof(storage->buffer));
    const int read = httpDownloadBody(http, sink, RESPONSE_TIMEOUT_MS);
    if (read < 0 || sink.overflowed()) {
      http.end();
      connection.close();
      return HTTPC_ERROR_READ_TIMEOUT;
    }
    storage->buffer[sink.length()] = '\0';
    responseLength = sink.length();
  }
  http.end();
  if (status <= 0) connection.close();
  return status;
}

bool fetchStates(Connection &connection, const char *haUrl, const char *token,
                 const char *label) {
  const size_t bodyLength = homeControlBuildTemplateRequest(
      label, storage->buffer, sizeof(storage->buffer));
  if (bodyLength == 0) {
    setMessage("Neplatný štítek ovládání");
    return false;
  }
  const String url = String(haUrl) + "/api/template";
  size_t responseLength = 0;
  const int status = post(connection, url.c_str(), token, bodyLength, responseLength);
  if (status != HTTP_CODE_OK) {
    char text[64];
    if (status == 401 || status == 403)
      strlcpy(text, "Token HA nemá práva správce", sizeof(text));
    else if (status <= 0)
      strlcpy(text, "Home Assistant neodpovídá", sizeof(text));
    else
      snprintf(text, sizeof(text), "Home Assistant vrátil %d", status);
    Serial.printf("[ovladani] cteni stavu selhalo (%d)\n", status);
    setMessage(text);
    return false;
  }
  const size_t count = homeControlParseResponse(
      storage->buffer, responseLength, storage->parsed, HOME_CONTROL_MAX_ITEMS);

  const unsigned long now = millis();
  bool timedOut = false;
  portENTER_CRITICAL(&stateMux);
  for (size_t index = 0; index < count; ++index) {
    HomeControlTile &tile = storage->tiles[index];
    const HomeControlItem &fresh = storage->parsed[index];
    // Dlaždice na stejném místě se stejnou entitou si podrží čekající stav,
    // dokud ho HA nepotvrdí nebo nevyprší.
    const bool sameEntity = index < tileCount && tile.pending &&
                            strcmp(tile.item.entityId, fresh.entityId) == 0;
    if (sameEntity && fresh.state != tile.item.state &&
        now - pendingSince[index] < PENDING_TIMEOUT_MS) {
      HomeControlState expected = tile.item.state;
      tile.item = fresh;
      tile.item.state = expected;
      continue;
    }
    if (sameEntity && fresh.state != tile.item.state) timedOut = true;
    tile.item = fresh;
    tile.pending = false;
    pendingSince[index] = 0;
  }
  for (size_t index = count; index < HOME_CONTROL_MAX_ITEMS; ++index)
    pendingSince[index] = 0;
  tileCount = static_cast<uint8_t>(count);
  haveData = true;
  lastSuccessAt = now;
  if (!timedOut) message[0] = '\0';
  ++generation;
  portEXIT_CRITICAL(&stateMux);
  if (timedOut) setMessage("Zařízení nepotvrdilo přepnutí");
  return true;
}

bool sendCommand(Connection &connection, const char *haUrl, const char *token,
                 const char *entityId, HomeControlDomain domain) {
  char path[32];
  if (homeControlServicePath(domain, path, sizeof(path)) == 0) return false;
  const size_t bodyLength =
      homeControlBuildServiceBody(entityId, storage->buffer, sizeof(storage->buffer));
  if (bodyLength == 0) return false;
  const String url = String(haUrl) + "/api/services/" + path;
  size_t responseLength = 0;
  const int status = post(connection, url.c_str(), token, bodyLength, responseLength);
  Serial.printf("[ovladani] %s %s -> %d\n", path, entityId, status);
  if (status != HTTP_CODE_OK) {
    setMessage(status == 401 || status == 403 ? "Token HA nesmí ovládat"
                                              : "Příkaz se nepodařilo odeslat");
    return false;
  }
  return true;
}

// Vrátí čekající dlaždice do skutečného stavu (příkaz neodešel).
void cancelPending() {
  portENTER_CRITICAL(&stateMux);
  for (uint8_t index = 0; index < tileCount; ++index) {
    if (storage->tiles[index].pending) pendingSince[index] = 1;
  }
  portEXIT_CRITICAL(&stateMux);
}

uint32_t backoffMs(uint8_t failures) {
  uint32_t delay = VISIBLE_PERIOD_MS;
  for (uint8_t step = 0; step < failures && delay < MAX_BACKOFF_MS; ++step)
    delay *= 2;
  return delay < MAX_BACKOFF_MS ? delay : MAX_BACKOFF_MS;
}

void homeControlTask(void *) {
  Connection *connection = new (std::nothrow) Connection();
  char url[CLOCK_HA_URL_LENGTH] = "";
  char token[CLOCK_HA_TOKEN_LENGTH] = "";
  char label[CLOCK_CONTROLS_LABEL_LENGTH] = "";
  char entity[HOME_CONTROL_ENTITY_ID_LENGTH] = "";
  uint32_t lastRevision = UINT32_MAX;
  bool wasVisible = false;
  uint8_t failures = 0;
  unsigned long nextFetchAt = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    if (connection == nullptr) continue;
    portENTER_CRITICAL(&stateMux);
    const bool wantVisible = visible;
    const uint32_t revision = requestRevision;
    strlcpy(url, requestUrl, sizeof(url));
    strlcpy(token, requestToken, sizeof(token));
    strlcpy(label, requestLabel, sizeof(label));
    const bool command = commandQueued;
    commandQueued = false;
    strlcpy(entity, commandEntity, sizeof(entity));
    const HomeControlDomain domain = commandDomain;
    portEXIT_CRITICAL(&stateMux);

    if (!wantVisible || url[0] == '\0' || token[0] == '\0') {
      if (wasVisible) connection->close();
      wasVisible = false;
      continue;
    }
    const unsigned long now = millis();
    if (!wasVisible || revision != lastRevision) {
      if (revision != lastRevision) connection->close();
      connection->secureUrl = strncmp(url, "https://", 8) == 0;
      if (connection->secureUrl) {
        // Stejně jako ostatní dotazy na HA: adresu zadává majitel a HA bývá
        // i za vlastním certifikátem.
        connection->secure.setInsecure();
        connection->secure.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
      }
      lastRevision = revision;
      wasVisible = true;
      failures = 0;
      nextFetchAt = now;
    }
    if (WiFi.status() != WL_CONNECTED) continue;

    if (command) {
      if (sendCommand(*connection, url, token, entity, domain)) {
        nextFetchAt = millis() + AFTER_COMMAND_READ_MS;
      } else {
        cancelPending();
        nextFetchAt = millis();
      }
      continue;
    }
    if (static_cast<long>(now - nextFetchAt) < 0) continue;

    portENTER_CRITICAL(&stateMux);
    loading = !haveData;
    portEXIT_CRITICAL(&stateMux);
    const bool ok = fetchStates(*connection, url, token, label);
    portENTER_CRITICAL(&stateMux);
    if (loading) ++generation;
    loading = false;
    portEXIT_CRITICAL(&stateMux);
    failures = ok ? 0 : static_cast<uint8_t>(failures < 8 ? failures + 1 : 8);
    nextFetchAt = millis() + (ok ? VISIBLE_PERIOD_MS : backoffMs(failures));
  }
}

}  // namespace

void homeControlServiceBegin() {
  if (taskHandle != nullptr) return;
  if (storage == nullptr) {
    void *memory = heap_caps_calloc(1, sizeof(Storage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) {
      Serial.println("[ovladani] neni PSRAM pro obrazovku Ovladani");
      return;
    }
    storage = new (memory) Storage();
  }
  xTaskCreatePinnedToCoreWithCaps(homeControlTask, "controls", 16384, nullptr, 1,
                                  &taskHandle, 0,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void homeControlServicePrepareForFirmwareUpdate() {
  portENTER_CRITICAL(&stateMux);
  visible = false;
  commandQueued = false;
  portEXIT_CRITICAL(&stateMux);
  if (taskHandle != nullptr) {
    // Spojení drží úloha; s ní zmizí i jeho TLS kontext (Connection je na haldě
    // a uniká, ale jen jednou za aktualizaci, po které se restartuje).
    vTaskDeleteWithCaps(taskHandle);
    taskHandle = nullptr;
  }
}

void homeControlServiceSetActive(bool nowVisible, const char *haUrl,
                                 const char *token, const char *label) {
  bool notify = false;
  portENTER_CRITICAL(&stateMux);
  const char *safeUrl = haUrl != nullptr ? haUrl : "";
  const char *safeToken = token != nullptr ? token : "";
  const char *safeLabel = label != nullptr ? label : "";
  if (strcmp(requestUrl, safeUrl) != 0 || strcmp(requestToken, safeToken) != 0 ||
      strcmp(requestLabel, safeLabel) != 0) {
    strlcpy(requestUrl, safeUrl, sizeof(requestUrl));
    strlcpy(requestToken, safeToken, sizeof(requestToken));
    strlcpy(requestLabel, safeLabel, sizeof(requestLabel));
    ++requestRevision;
    // Jiný štítek nebo HA: staré dlaždice by patřily jinému seznamu.
    tileCount = 0;
    haveData = false;
    ++generation;
    notify = true;
  }
  if (visible != nowVisible) {
    visible = nowVisible;
    notify = true;
  }
  portEXIT_CRITICAL(&stateMux);
  if (notify && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
}

bool homeControlServiceSnapshot(HomeControlSnapshot &snapshot,
                                uint32_t knownGeneration) {
  if (storage == nullptr) return false;
  bool copied = false;
  portENTER_CRITICAL(&stateMux);
  if (generation != knownGeneration) {
    snapshot.generation = generation;
    snapshot.haveData = haveData;
    snapshot.loading = loading;
    snapshot.lastSuccessAt = lastSuccessAt;
    strlcpy(snapshot.message, message, sizeof(snapshot.message));
    snapshot.count = tileCount;
    memcpy(snapshot.tiles, storage->tiles, sizeof(HomeControlTile) * tileCount);
    copied = true;
  }
  portEXIT_CRITICAL(&stateMux);
  return copied;
}

bool homeControlServicePress(uint8_t index) {
  if (storage == nullptr) return false;
  bool accepted = false;
  portENTER_CRITICAL(&stateMux);
  if (index < tileCount && !commandQueued) {
    HomeControlTile &tile = storage->tiles[index];
    if (tile.item.state != HomeControlState::Unavailable) {
      strlcpy(commandEntity, tile.item.entityId, sizeof(commandEntity));
      commandDomain = tile.item.domain;
      commandQueued = true;
      if (tile.item.state == HomeControlState::On ||
          tile.item.state == HomeControlState::Off) {
        tile.item.state = tile.item.state == HomeControlState::On
                              ? HomeControlState::Off
                              : HomeControlState::On;
        tile.pending = true;
        pendingSince[index] = millis();
        if (pendingSince[index] == 0) pendingSince[index] = 1;
      }
      ++generation;
      accepted = true;
    }
  }
  portEXIT_CRITICAL(&stateMux);
  if (accepted && taskHandle != nullptr) xTaskNotifyGive(taskHandle);
  return accepted;
}

bool homeControlServiceHasData() {
  portENTER_CRITICAL(&stateMux);
  const bool result = haveData && tileCount > 0;
  portEXIT_CRITICAL(&stateMux);
  return result;
}
