#pragma once

#include <Arduino.h>

#include "ClockConfig.h"
#include "SatelliteFeed.h"
#include "SkyFeed.h"
#include "SkyRender.h"

// Obrazovka družic: obloha nad hodinami jako kruh, zenit uprostřed, obzor na
// okraji. Dráhy počítá vlastní server (infra/satellites) z dat CelesTraku;
// služba se ho ptá jednou za minutu a mezi dotazy polohu každou vteřinu
// dopočítá z dráhy, kterou server poslal na tři minuty dopředu.
//
// Kreslí se stejně jako radar letadel: služba vykreslí oblohu i družice do
// bufferu RGB565 a obrazovka ho jen podloží pod LVGL canvas. Text kolem (čas,
// počet družic, přelet družice, detail) kreslí ClockDashboard, protože umí
// diakritiku.

constexpr uint16_t SATELLITE_SKY_WIDTH = 480;
constexpr uint16_t SATELLITE_SKY_HEIGHT = 480;

struct SatelliteDetail {
  bool open = false;
  // Družice zapadla pod nastavenou výšku nebo zmizela z dat; čísla jsou
  // poslední známá.
  bool lost = false;
  char name[SATELLITE_NAME_LENGTH] = "";
  uint32_t noradId = 0;
  uint8_t group = 0;
  float azimuthDeg = 0.0f;
  float elevationDeg = 0.0f;
  uint16_t altitudeKm = 0;
  uint16_t rangeKm = 0;
  bool sunlit = false;
  // Na Slunci, nad obzorem a pozorovatel ve tmě: jde vidět okem.
  bool visibleNow = false;
};

struct SatelliteSnapshot {
  const uint16_t *pixels = nullptr;
  uint32_t generation = 0;
  // Snímek je noční obloha, ne družice (druhá stránka obrazovky).
  bool skyPage = false;
  bool loading = false;
  // Snímek je kreslený z dat, která pokrývají tuhle chvíli.
  bool haveData = false;
  uint8_t shownCount = 0;
  uint8_t visibleCount = 0;
  // Slunce aspoň šest stupňů pod obzorem; jen tehdy má smysl psát, kolik
  // družic jde vidět.
  bool observerDark = false;
  // Přelet na řádek pod oblohou; neplatný, když žádný nezbyl nebo je skupina
  // té družice vypnutá. Vybírá ho satellitePickPass().
  SatellitePass pass;
  char message[64] = "";
  SatelliteDetail detail;
};

struct SatelliteDiagnostics {
  bool available = false;
  bool active = false;
  bool visible = false;
  bool loading = false;
  bool haveData = false;
  uint16_t trackCount = 0;
  uint16_t serverTotal = 0;
  uint16_t elementAgeHours = 0;
  uint32_t lastSuccessfulRefreshAgeMs = 0;
  uint32_t nextRefreshInMs = 0;
  int lastHttpStatus = 0;
  size_t lastDownloadedBytes = 0;
  char message[64] = "";
  char serverProblem[64] = "";
};

// --- Noční obloha -------------------------------------------------------------
// Druhá stránka obrazovky družic: planety, Měsíc a jasné hvězdy ve stejném
// kruhu, nahoře index Kp a dole nejbližší úkazy. Obloha i planety se kreslí do
// téhož bufferu jako družice; jména těles píše ClockDashboard, protože umí
// diakritiku, a služba mu jen řekne, kam.

struct SkySnapshot {
  uint32_t generation = 0;
  bool loading = false;
  // Data jsou čerstvá a snímek z nich nakreslený.
  bool haveData = false;
  // Slunce aspoň šest stupňů pod obzorem, nebo nad ním.
  bool dark = false;
  bool sunUp = false;
  // Astronomická tma (0, když není) a nejbližší východ a západ Měsíce.
  int64_t darkFrom = 0;
  int64_t darkTo = 0;
  // Den od východu do západu Slunce (0 od staršího serveru).
  int64_t dayFrom = 0;
  int64_t dayTo = 0;
  bool hasMoon = false;
  int64_t moonRise = 0;
  int64_t moonSet = 0;
  float moonIllumination = 0.0f;
  bool hasKp = false;
  float kp = 0.0f;
  bool hasKpMax = false;
  float kpMax = 0.0f;
  int64_t kpMaxAt = 0;
  SkyEvent events[SKY_MAX_EVENTS];
  uint8_t eventCount = 0;
  SkyLabel labels[SKY_MAX_BODIES];
  uint8_t labelCount = 0;
  SkyDetail detail;
  char message[64] = "";
};

// Stránka zapnutá v nastavení (enabled) a právě ukázaná (shown). Se zapnutou
// stránkou se obloha stahuje i se schovanou obrazovkou, aby hodiny mohly
// upozornit na polární záři.
// hideEvents: bez řádků úkazů, jejich pás dostanou popisky oblohy.
void satelliteServiceSetNightSky(bool enabled, bool shown, bool hideEvents);
void satelliteServiceSkySnapshot(SkySnapshot &snapshot);
// Poslední čerstvý index Kp (odhad NOAA po minutě); false, když není.
bool satelliteServiceAuroraKp(float &kp);

// Zkouška adresy z webu: stáhne odpověď a vybere družice nejvýš nad obzorem.
constexpr size_t SATELLITE_PROBE_ENTRIES = 12;

struct SatelliteProbeEntry {
  char name[SATELLITE_NAME_LENGTH] = "";
  uint8_t group = 0;
  float azimuthDeg = 0.0f;
  float elevationDeg = 0.0f;
  bool sunlit = false;
};

struct SatelliteProbeResult {
  bool ok = false;
  int httpStatus = 0;
  char error[96] = "";
  uint16_t count = 0;
  uint16_t serverTotal = 0;
  uint16_t elementAgeHours = 0;
  bool observerDark = false;
  bool pending = false;
  char problem[64] = "";
  SatellitePass passes[SATELLITE_MAX_PASSES];
  uint8_t passCount = 0;
  uint8_t entryCount = 0;
  SatelliteProbeEntry entries[SATELLITE_PROBE_ENTRIES];
};

void satelliteServiceBegin();
void satelliteServicePrepareForFirmwareUpdate();

// Zapne nebo vypne obrazovku. backgroundRefresh drží řídké stahování i se
// schovanou obrazovkou, aby ji automatické střídání mělo s čím otevřít.
// english mění jen písmena světových stran v kresbě.
void satelliteServiceSetActive(bool visible, bool backgroundRefresh,
                               float latitude, float longitude,
                               const ClockSatellitesConfig &config,
                               bool english);
void satelliteServiceSetRedNightMode(bool enabled);
void satelliteServiceSnapshot(SatelliteSnapshot &snapshot);
void satelliteServiceDiagnostics(SatelliteDiagnostics &diagnostics);

// Má služba dráhy, které pokrývají tuhle chvíli? Podle toho ji pustí střídání.
bool satelliteServiceHasCurrentData();

// Krátké klepnutí: s otevřeným detailem ho zavře, jinak vybere nejbližší
// družici. Vrací true, když se něco změnilo.
bool satelliteServiceHandleTap(int16_t x, int16_t y);
bool satelliteServiceDetailOpen();
void satelliteServiceCloseDetail();

// Zkouška adresy. Provede ji úloha služby, protože má zásobník na TLS se
// svazkem kořenů Mozilly. Start vrací false, když úloha neběží nebo už jedna
// zkouška probíhá; výsledek je hotový, jakmile satelliteServiceProbeResult()
// vrátí true.
bool satelliteServiceStartProbe(const ClockSatellitesConfig &config,
                                float latitude, float longitude);
bool satelliteServiceProbeResult(SatelliteProbeResult &result);
// Zruší čekání na výsledek (web to vzdal); úloha doběhne a výsledek zahodí.
void satelliteServiceAbandonProbe();
