#pragma once

#include <Arduino.h>

#include "AdsbParser.h"
#include "ClockConfig.h"
#include "RouteParser.h"

// Radar letadel. Data vozí adsb.fi, trasu vybraného letu adsb.lol; obojí je
// veřejné API zdarma, takže se s dotazy šetří. Převzato z projektu
// MeteoPlaneRadar (viz THIRD_PARTY_NOTICES.md) a přestavěno na zdejší způsob
// kreslení: služba si sama vykreslí mapu i letadla do bufferu RGB565, který
// obrazovka jen podloží pod LVGL canvas - stejně jako to dělá meteoradar.
//
// Text kolem mapy (čas, počet letadel, dosah, detail letadla) kreslí
// ClockDashboard jako LVGL popisky, protože ty umí pořádné písmo i diakritiku.
// Do bufferu jde jen to, co je opravdu grafika.

constexpr uint16_t PLANE_RADAR_WIDTH = 480;
constexpr uint16_t PLANE_RADAR_HEIGHT = 480;

// Stav detailu vybraného letadla. Obrazovka si z něj skládá popisky sama, aby
// se přepnutím jazyka přeložily i ony.
struct PlaneRadarDetail {
  bool open = false;
  // Letadlo dočasně vypadlo z dat a hodnoty jsou poslední známé. adsb.fi občas
  // jedno stahování vynechá a v dalším ho pošle zas, takže se panel nezavírá
  // hned - jen se přizná, že čísla nejsou živá.
  bool signalLost = false;
  char callsign[10] = "";
  char hex[8] = "";
  char type[10] = "";
  // Typ vypsaný slovy ("AIRBUS A-321neo"). Prázdné u letadel, která server ve
  // své databázi nenajde.
  char description[40] = "";
  char registration[12] = "";
  char squawk[6] = "";
  // Kód nouze, kterým letadlo vysílá, jinak prázdné.
  char emergency[6] = "";
  bool hasTrack = false;
  float trackDeg = 0.0f;
  float altitudeFt = 0.0f;
  float groundSpeedKt = 0.0f;
  float verticalRateFtMin = 0.0f;
  // Trasa. Vyplněná je jen ve stavu Known; jinak se místo ní píše, že se hledá
  // nebo že žádná není.
  PlaneRouteState routeState = PlaneRouteState::Pending;
  RouteInfo route;
};

struct PlaneRadarSnapshot {
  const uint16_t *pixels = nullptr;
  uint32_t generation = 0;
  bool loading = false;
  bool ready = false;
  // Kolik letadel prošlo filtrem a je vidět v kruhu.
  uint8_t shownCount = 0;
  // Je mezi nimi hlídaný let?
  bool watchedVisible = false;
  // Nejzávažnější nouzový kód na obrazovce, jinak prázdné.
  char emergency[6] = "";
  uint16_t rangeKm = 25;
  char message[64] = "";
  PlaneRadarDetail detail;
};

struct PlaneRadarDiagnostics {
  // Obrazovka je zapnutá v nastavení. Že se zrovna stahuje, je něco jiného.
  bool available = false;
  // active znamená "stahuje se", tedy obrazovka je vidět NEBO je zapojená do
  // střídání. Jestli je zrovna na displeji, říká visible - jsou to dvě různé
  // věci a diagnostika je nesmí ukazovat jako jednu.
  bool active = false;
  bool visible = false;
  bool loading = false;
  bool ready = false;
  uint8_t aircraftCount = 0;
  uint16_t rangeKm = 25;
  uint32_t lastSuccessfulRefreshAgeMs = 0;
  uint32_t nextRefreshInMs = 0;
  int lastHttpStatus = 0;
  size_t lastDownloadedBytes = 0;
  int lastRouteHttpStatus = 0;
  char message[64] = "";
  char serverMessage[48] = "";
};

void planeRadarServiceBegin();
void planeRadarServicePrepareForFirmwareUpdate();

// Zapne nebo vypne obrazovku. backgroundRefresh drží stahování i tehdy, když
// je radar zrovna schovaný, aby se po přepnutí neukazovala prázdná obloha -
// stejně jako u meteoradaru.
// feedUrl je nepovinná adresa vlastního zdroje letadel. Prázdná znamená ptát se
// adsb.fi přímo, tedy chování bez serveru; vyplněná ukazuje na infra/planes,
// které tutéž odpověď ořeže na to, co firmware opravdu čte.
void planeRadarServiceSetActive(bool visible, bool backgroundRefresh,
                                float latitude, float longitude,
                                const ClockPlanesConfig &planes,
                                const char *feedUrl);
void planeRadarServiceSetRedNightMode(bool enabled);
void planeRadarServiceSnapshot(PlaneRadarSnapshot &snapshot);
void planeRadarServiceDiagnostics(PlaneRadarDiagnostics &diagnostics);

// Krátké klepnutí. S otevřeným detailem ho jakékoli klepnutí zavře, jinak se
// vybere nejbližší letadlo. Vrací true, když se stav změnil a má se překreslit.
bool planeRadarServiceHandleTap(int16_t x, int16_t y);

// Změna dosahu o krok. Při otevřeném detailu se ignoruje - nejdřív se zavírá.
void planeRadarServiceChangeRange(int8_t direction);

bool planeRadarServiceDetailOpen();
// Zavře detail; používá to podržení prstu, kterým se odchází na jinou
// obrazovku.
void planeRadarServiceCloseDetail();
