#include "FirmwareUpdateService.h"

#include <HTTPClient.h>
#include <atomic>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <freertos/idf_additions.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include "Board.h"
#include "FirmwareBuild.h"
#include "FirmwareHubCa.h"
#include "NetworkCoordinator.h"
#include "OtaMetadata.h"
#include "SemVer.h"

namespace {
constexpr time_t VALID_TIME_THRESHOLD = 1700000000;
constexpr uint32_t NETWORK_TIMEOUT_MS = 15000;
constexpr size_t DOWNLOAD_BUFFER_SIZE = 4096;

SemaphoreHandle_t statusMutex = nullptr;
FirmwareUpdateSnapshot status;
FirmwareUpdateLifecycleCallback lifecycleCallback = nullptr;
// Kontrola běží v jedné trvalé úloze, která čeká na požadavek. Dřív se pro
// každou kontrolu zakládala nová úloha se zásobníkem v PSRAM a na konci se
// sama smazala přes vTaskDeleteWithCaps(nullptr). ESP-IDF to nedoporučuje
// a pomocná úloha prvTaskDeleteWithCapsTask při tom 24. 9. 2026 na barvpravo
// spadla (panika po `clock-sync.py check`).
TaskHandle_t checkTaskHandle = nullptr;
std::atomic<bool> restartPending{false};
std::atomic<bool> loopParked{false};
// Nejdéle, kolik se čeká na hlavní smyčku. Ověření hesla webu (PBKDF2) trvá
// zlomek sekundy; kdyby smyčka visela jinde, restart proběhne i tak.
constexpr uint32_t LOOP_PARK_TIMEOUT_MS = 3000;
bool checkInstallRequested = false;  // pod statusMutex

struct PendingFirmwareInstall {
  char url[256] = "";
  char sha256[65] = "";
  uint32_t size = 0;
};

PendingFirmwareInstall pendingInstall;

void lockStatus() {
  if (statusMutex != nullptr) xSemaphoreTake(statusMutex, portMAX_DELAY);
}

void unlockStatus() {
  if (statusMutex != nullptr) xSemaphoreGive(statusMutex);
}

void setMessage(FirmwareUpdateState state, const char *message, bool busy) {
  lockStatus();
  status.state = state;
  status.busy = busy;
  strlcpy(status.message, message == nullptr ? "" : message,
          sizeof(status.message));
  unlockStatus();
}

// Adresa metadat podle desky (OtaMetadata). Kulatá 7" obraz nikdy neuvidí,
// protože čte jiný soubor; pole "board" v metadatech a značka desky v obrazu
// jsou další pojistky.
String metadataUrl() {
  char url[256];
  if (!otaMetadataUrl(FIRMWARE_SERVER_URL, FIRMWARE_OTA_METADATA_PATH,
                      FIRMWARE_PROJECT_SLUG, BOARD_ID, url, sizeof(url)))
    return String();
  return String(url);
}

String sha256Hex(const uint8_t digest[32]) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  char result[65];
  for (size_t index = 0; index < 32; ++index) {
    result[index * 2] = HEX_DIGITS[digest[index] >> 4];
    result[index * 2 + 1] = HEX_DIGITS[digest[index] & 0x0F];
  }
  result[64] = '\0';
  return String(result);
}

bool installFirmware(const String &url, uint32_t expectedSize,
                     const String &expectedSha256) {
  NetworkOperationGuard networkGuard(NETWORK_TIMEOUT_MS);
  if (!networkGuard) {
    setMessage(FirmwareUpdateState::Failed,
               "Síť je právě vytížená jinou operací.", false);
    return false;
  }
  setMessage(FirmwareUpdateState::Downloading,
             "Stahuji a ověřuji nový firmware…", true);
  lockStatus();
  status.downloadedBytes = 0;
  status.totalBytes = expectedSize;
  unlockStatus();
  if (lifecycleCallback != nullptr) lifecycleCallback(true);

  WiFiClientSecure client;
  client.setCACert(FIRMWARE_RELEASE_ROOT_CA);
  client.setTimeout(NETWORK_TIMEOUT_MS);
  client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setConnectTimeout(NETWORK_TIMEOUT_MS);
  http.setTimeout(NETWORK_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    setMessage(FirmwareUpdateState::Failed,
               "Nepodařilo se otevřít OTA adresu.", false);
    if (lifecycleCallback != nullptr) lifecycleCallback(false);
    return false;
  }
  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    String detail;
    if (responseCode < 0) {
      char tlsError[160] = {};
      client.lastError(tlsError, sizeof(tlsError));
      detail = tlsError[0] != '\0' ? tlsError
                                   : HTTPClient::errorToString(responseCode);
    } else {
      detail = String("HTTP ") + responseCode;
    }
    http.end();
    client.stop();
    const String message =
        String("Server nevydal aplikační OTA obraz: ") + detail + ".";
    setMessage(FirmwareUpdateState::Failed, message.c_str(), false);
    if (lifecycleCallback != nullptr) lifecycleCallback(false);
    return false;
  }
  const int contentLength = http.getSize();
  if (contentLength < 0 || static_cast<uint32_t>(contentLength) != expectedSize) {
    http.end();
    setMessage(FirmwareUpdateState::Failed,
               "Velikost OTA obrazu neodpovídá metadatům.", false);
    if (lifecycleCallback != nullptr) lifecycleCallback(false);
    return false;
  }
  if (!Update.begin(expectedSize, U_FLASH)) {
    http.end();
    setMessage(FirmwareUpdateState::Failed,
               "OTA obraz se nevejde do neaktivního slotu.", false);
    if (lifecycleCallback != nullptr) lifecycleCallback(false);
    return false;
  }

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts(&shaContext, 0);
  NetworkClient *stream = http.getStreamPtr();
  uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
  uint32_t received = 0;
  unsigned long lastDataAt = millis();
  bool writeOk = true;
  // Značka desky v obrazu: zachytí release, do kterého se omylem zabalil
  // obraz druhé desky (metadata i SHA-256 by pak k němu seděla).
  OtaBoardTagScanner boardTag;
  while (received < expectedSize) {
    const int available = stream->available();
    if (available <= 0) {
      if (!http.connected() || millis() - lastDataAt >= NETWORK_TIMEOUT_MS) {
        writeOk = false;
        break;
      }
      delay(1);
      continue;
    }
    const size_t wanted =
        min(static_cast<size_t>(available),
            min(sizeof(buffer), static_cast<size_t>(expectedSize - received)));
    const int count = stream->readBytes(buffer, wanted);
    if (count <= 0 || Update.write(buffer, count) != static_cast<size_t>(count)) {
      writeOk = false;
      break;
    }
    mbedtls_sha256_update(&shaContext, buffer, count);
    boardTag.feed(buffer, count);
    received += count;
    lastDataAt = millis();
    lockStatus();
    status.downloadedBytes = received;
    unlockStatus();
  }

  uint8_t digest[32];
  mbedtls_sha256_finish(&shaContext, digest);
  mbedtls_sha256_free(&shaContext);
  http.end();
  String actualSha256 = sha256Hex(digest);
  actualSha256.toLowerCase();
  String requiredSha256 = expectedSha256;
  requiredSha256.toLowerCase();
  if (!writeOk || received != expectedSize || actualSha256 != requiredSha256) {
    Update.abort();
    setMessage(FirmwareUpdateState::Failed,
               actualSha256 == requiredSha256
                   ? "Stažení OTA obrazu nebylo dokončeno."
                   : "Kontrolní SHA-256 OTA obrazu nesouhlasí.",
               false);
    if (lifecycleCallback != nullptr) lifecycleCallback(false);
    return false;
  }
  if (!boardTag.accepted()) {
    Update.abort();
    setMessage(FirmwareUpdateState::Failed,
               boardTag.otherTagSeen()
                   ? "OTA obraz je pro jinou desku."
                   : "OTA obraz nenese značku této desky.",
               false);
    if (lifecycleCallback != nullptr) lifecycleCallback(false);
    return false;
  }
  if (!Update.end(true)) {
    Update.abort();
    setMessage(FirmwareUpdateState::Failed,
               "OTA obraz se nepodařilo aktivovat.", false);
    if (lifecycleCallback != nullptr) lifecycleCallback(false);
    return false;
  }

  setMessage(FirmwareUpdateState::Restarting,
             "Aktualizace je ověřená, zařízení se restartuje…", true);
  delay(750);
  restartPending = true;
  const unsigned long parkStarted = millis();
  while (!loopParked && millis() - parkStarted < LOOP_PARK_TIMEOUT_MS) delay(5);
  ESP.restart();
  return true;
}

bool checkFirmware(bool installWhenAvailable) {
  if (WiFi.status() != WL_CONNECTED) {
    setMessage(FirmwareUpdateState::Failed,
               "Zařízení není připojené k Wi-Fi.", false);
    return false;
  }
  if (FIRMWARE_SERVER_URL[0] == '\0' || FIRMWARE_PROJECT_SLUG[0] == '\0') {
    setMessage(FirmwareUpdateState::Failed,
               "Server aktualizací není v buildu nakonfigurovaný.", false);
    return false;
  }
  if (time(nullptr) < VALID_TIME_THRESHOLD) {
    setMessage(FirmwareUpdateState::Failed,
               "Čas ještě není synchronizovaný pro bezpečné HTTPS.", false);
    return false;
  }

  NetworkOperationGuard networkGuard(NETWORK_TIMEOUT_MS);
  if (!networkGuard) {
    setMessage(FirmwareUpdateState::Failed,
               "Síť je právě vytížená jinou operací.", false);
    return false;
  }

  String payload;
  {
    const String metadataEndpoint = metadataUrl();
    if (metadataEndpoint.isEmpty()) {
      setMessage(FirmwareUpdateState::Failed,
                 "Adresa OTA metadat je příliš dlouhá.", false);
      return false;
    }
    WiFiClientSecure client;
    client.setCACert(FIRMWARE_RELEASE_ROOT_CA);
    client.setTimeout(NETWORK_TIMEOUT_MS);
    client.setHandshakeTimeout(NETWORK_TLS_HANDSHAKE_TIMEOUT_S);
    HTTPClient http;
    http.setConnectTimeout(NETWORK_TIMEOUT_MS);
    http.setTimeout(NETWORK_TIMEOUT_MS);
    if (!http.begin(client, metadataEndpoint)) {
      setMessage(FirmwareUpdateState::Failed,
                 "Nepodařilo se otevřít server aktualizací.", false);
      return false;
    }
    http.addHeader("Accept", "application/json");
    const int responseCode = http.GET();
    if (responseCode == HTTP_CODE_NOT_FOUND) {
      http.end();
      client.stop();
      setMessage(FirmwareUpdateState::Failed,
                 "Server zatím nemá OTA metadata pro tento projekt.",
                 false);
      return false;
    }
    if (responseCode != HTTP_CODE_OK) {
      String detail;
      if (responseCode < 0) {
        char tlsError[160] = {};
        client.lastError(tlsError, sizeof(tlsError));
        detail = tlsError[0] != '\0' ? tlsError
                                     : HTTPClient::errorToString(responseCode);
      } else {
        detail = String("HTTP ") + responseCode;
      }
      http.end();
      client.stop();
      const String message =
          String("Kontrola verze na serveru selhala: ") + detail + ".";
      setMessage(FirmwareUpdateState::Failed, message.c_str(), false);
      return false;
    }
    payload = http.getString();
    http.end();
    client.stop();
  }

  OtaMetadata metadata;
  switch (otaParseMetadata(payload.c_str(), payload.length(),
                           FIRMWARE_SERVER_URL, FIRMWARE_CHIP_VARIANT,
                           BOARD_ID, metadata)) {
    case OtaMetadataResult::Ok:
      break;
    case OtaMetadataResult::WrongBoard:
      setMessage(FirmwareUpdateState::Failed,
                 "OTA obraz ze serveru je pro jinou desku.", false);
      return false;
    case OtaMetadataResult::DisallowedUrl:
      setMessage(FirmwareUpdateState::Failed,
                 "OTA obraz neleží na povoleném serveru.", false);
      return false;
    case OtaMetadataResult::Invalid:
    default:
      setMessage(FirmwareUpdateState::Failed,
                 "OTA metadata ze serveru nejsou platná.", false);
      return false;
  }

  const bool newer =
      !IS_RELEASE_FIRMWARE || semVerCompare(FIRMWARE_VERSION, metadata.version) < 0;
  lockStatus();
  strlcpy(status.serverVersion, metadata.version, sizeof(status.serverVersion));
  status.totalBytes = metadata.size;
  status.downloadedBytes = 0;
  status.updateAvailable = newer;
  unlockStatus();
  if (!newer) {
    setMessage(FirmwareUpdateState::Current,
               "Používáš aktuální verzi firmware.", false);
    return false;
  }
  if (!installWhenAvailable) {
    setMessage(FirmwareUpdateState::Available,
               IS_RELEASE_FIRMWARE
                   ? "Na serveru je dostupná novější verze."
                   : "Novější release je dostupný; development se aktualizuje kabelem.",
               false);
    return false;
  }
  if (!IS_RELEASE_FIRMWARE) {
    setMessage(FirmwareUpdateState::Available,
               "Development build se aktualizuje pouze kabelem.", false);
    return false;
  }
  lockStatus();
  strlcpy(pendingInstall.url, metadata.url, sizeof(pendingInstall.url));
  strlcpy(pendingInstall.sha256, metadata.sha256,
          sizeof(pendingInstall.sha256));
  pendingInstall.size = metadata.size;
  unlockStatus();
  return true;
}

void installTask(void *) {
  PendingFirmwareInstall request;
  lockStatus();
  request = pendingInstall;
  unlockStatus();
  installFirmware(String(request.url), request.size, String(request.sha256));
  vTaskDelete(nullptr);
}

void updateCheckTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    lockStatus();
    const bool installWhenAvailable = checkInstallRequested;
    unlockStatus();
    const bool installRequested = checkFirmware(installWhenAvailable);
    // Instalace má vlastní úlohu se zásobníkem v interní RAM: zápis do flash
    // ze zásobníku v PSRAM skončí assertem. Ta se maže obyčejným vTaskDelete,
    // což u úlohy bez WithCaps nevadí.
    if (installRequested &&
        xTaskCreatePinnedToCore(installTask, "firmware-install", 12288, nullptr,
                                1, nullptr, 0) != pdPASS) {
      setMessage(FirmwareUpdateState::Failed,
                 "Instalační OTA úlohu se nepodařilo spustit.", false);
    }
  }
}
}  // namespace

// Jádro by nový obraz potvrdilo hned v initArduino(), ještě před setup(), a
// obraz padající při startu by pak bez USB nešlo vrátit. Potvrzuje se proto
// až z loop() po ustálení (firmwareUpdateServiceConfirmHealthyBoot).
extern "C" bool verifyRollbackLater() { return true; }

void firmwareUpdateServiceConfirmHealthyBoot() {
  // Dvě minuty pokryjí start, Wi-Fi, první načtení obrazovek i radaru.
  constexpr uint32_t HEALTHY_UPTIME_MS = 120000;
  static bool confirmed = false;
  if (confirmed || millis() < HEALTHY_UPTIME_MS) return;
  confirmed = true;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) ==
          ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

void firmwareUpdateServiceParkLoopBeforeRestart() {
  if (!restartPending) return;
  loopParked = true;
  for (;;) delay(10);
}

void firmwareUpdateServiceBegin(FirmwareUpdateLifecycleCallback callback) {
  lifecycleCallback = callback;
  if (statusMutex == nullptr) statusMutex = xSemaphoreCreateMutex();
  lockStatus();
  status = FirmwareUpdateSnapshot{};
  strlcpy(status.currentVersion, FIRMWARE_VERSION,
          sizeof(status.currentVersion));
  strlcpy(status.message,
          IS_RELEASE_FIRMWARE
              ? "Aktualizace zatím nebyla zkontrolována."
              : "Development build se nadále nahrává přes USB.",
          sizeof(status.message));
  status.installationSupported = IS_RELEASE_FIRMWARE;
  unlockStatus();
}

bool firmwareUpdateServiceRequestCheck(bool installWhenAvailable) {
  lockStatus();
  if (status.busy) {
    unlockStatus();
    return false;
  }
  status.busy = true;
  status.state = FirmwareUpdateState::Checking;
  strlcpy(status.message, "Kontroluji novou verzi…",
          sizeof(status.message));
  unlockStatus();
  lockStatus();
  checkInstallRequested = installWhenAvailable;
  unlockStatus();
  // Kontrola metadat nezapisuje do flash, proto může mít zásobník v PSRAM a
  // ponechat interní RAM TLS handshaku. Samotnou instalaci po kontrole převezme
  // oddělený task s interním zásobníkem, který je bezpečný během zápisu flash.
  if (checkTaskHandle == nullptr &&
      xTaskCreatePinnedToCoreWithCaps(updateCheckTask, "firmware-check", 12288,
                                      nullptr, 1, &checkTaskHandle, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    checkTaskHandle = nullptr;
    setMessage(FirmwareUpdateState::Failed,
               "OTA úlohu se nepodařilo spustit.", false);
    return false;
  }
  xTaskNotifyGive(checkTaskHandle);
  return true;
}

FirmwareUpdateSnapshot firmwareUpdateServiceSnapshot() {
  lockStatus();
  const FirmwareUpdateSnapshot snapshot = status;
  unlockStatus();
  return snapshot;
}

const char *firmwareUpdateStateName(FirmwareUpdateState state) {
  switch (state) {
    case FirmwareUpdateState::Idle: return "idle";
    case FirmwareUpdateState::Checking: return "checking";
    case FirmwareUpdateState::Available: return "available";
    case FirmwareUpdateState::Current: return "current";
    case FirmwareUpdateState::Downloading: return "downloading";
    case FirmwareUpdateState::Failed: return "failed";
    case FirmwareUpdateState::Restarting: return "restarting";
  }
  return "unknown";
}
