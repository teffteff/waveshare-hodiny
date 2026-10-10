#pragma once

#include <Arduino.h>

#include "Board.h"

// Kulatý 2,1" kreslí radar do čtverce 480 x 480 s kruhovým výřezem. Na 7"
// je mapa obdélník 640 x 480 vlevo od úzkého sloupce, bez kruhu. Snímky se
// tam drží v polovičním rozlišení 320 x 240 a zobrazují se dvakrát zvětšené:
// knihovny ESP-IDF, se kterými se obraz 7" neposouvá, berou 3 MB PSRAM na
// kód a celé rozlišení by se do zbytku nevešlo. Data ČHMÚ mají kolem 1 km na
// pixel, takže ani v rozsahu 100 km se poloviční rozlišení neztrácí.
constexpr uint16_t CHMI_RADAR_WIDTH = HODINY_BOARD_LCD7 ? 320 : 480;
constexpr uint16_t CHMI_RADAR_HEIGHT = HODINY_BOARD_LCD7 ? 240 : 480;
constexpr uint8_t CHMI_RADAR_ZOOM = HODINY_BOARD_LCD7 ? 2 : 1;

struct ChmiRadarSnapshot {
  const uint16_t *pixels = nullptr;
  uint32_t generation = 0;
  uint32_t completedAnimationCycles = 0;
  bool loading = false;
  bool ready = false;
  bool fullPreparationInProgress = false;
  bool latestFrame = false;
  uint8_t currentFrameNumber = 0;
  uint8_t animationFrameCount = 0;
  uint16_t radiusKm = 50;
  // U RainVieweru jde o poloměr, který vybrané přiblížení opravdu dává;
  // mocniny dvou nepadnou přesně na nastavený rozsah.
  uint16_t effectiveRadiusKm = 50;
  bool rainViewerSource = false;
  // Srážková vrstva je vypnutá: radar nic nestahuje a ukazuje jen mapu,
  // případně s blesky. Taková mapa se počítá za hotový statický snímek.
  bool mapOnly = false;
  char frameTime[6] = "";
  char message[64] = "Čekám na otevření radaru";
};

struct ChmiRadarDiagnostics {
  bool active = false;
  bool loading = false;
  bool ready = false;
  bool preparationInProgress = false;
  bool lastSuccessfulRefreshAvailable = false;
  uint8_t requestedFrameCount = 0;
  uint8_t preparedFrameCount = 0;
  uint8_t animationFrameCount = 0;
  uint8_t pendingRefreshCount = 0;
  uint16_t radiusKm = 50;
  uint32_t lastSuccessfulRefreshAgeMs = 0;
  uint32_t nextRefreshInMs = 0;
  int lastHttpStatus = 0;
  size_t lastDownloadedBytes = 0;
  int lastDecodeResult = 0;
  uint16_t lastDecodedLineCount = 0;
  bool acceptedCompleteDecodeError = false;
  char latestIndexFile[64] = "";
  char currentFile[64] = "";
  char oldestFrameTime[6] = "";
  char newestFrameTime[6] = "";
  char message[64] = "";
};

void chmiRadarServiceBegin();
void chmiRadarServicePrepareForFirmwareUpdate();
void chmiRadarServiceSetActive(bool visible, bool backgroundRefresh,
                               float latitude, float longitude,
                               uint16_t radiusKm, uint8_t frameCount,
                               uint8_t mapOpacity, uint8_t pauseSeconds,
                               bool showLegend, bool showPrecipitation,
                               uint8_t source);
void chmiRadarServiceSetRedNightMode(bool enabled);
// Údery blesků přes radar a kruh výstrahy kolem polohy. Údery samotné drží
// LightningService; radar si je při kreslení každého snímku jen přečte.
void chmiRadarServiceSetLightning(bool overlay, float alarmLatitude,
                                  float alarmLongitude, uint8_t alarmRadiusKm,
                                  uint8_t alarmMinutes);
// Kruh, který radar s daným nastavením opravdu ukazuje - aby se služba blesků
// ptala na údery v celém viditelném výřezu, i u pohledu na celou republiku.
void chmiRadarViewCircle(float latitude, float longitude, uint16_t radiusKm,
                         float &centerLatitude, float &centerLongitude,
                         float &viewRadiusKm);
void chmiRadarServiceSnapshot(ChmiRadarSnapshot &snapshot);
void chmiRadarServiceDiagnostics(ChmiRadarDiagnostics &diagnostics);
