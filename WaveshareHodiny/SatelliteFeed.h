#pragma once

#include <stddef.h>
#include <stdint.h>

// Odpověď vlastního serveru družic (infra/satellites), oddělená od stahování,
// aby šla testovat na počítači.
//
// Server pošle pro každou družici nad obzorem krátkou dráhu po obloze: azimut
// a výšku v desetinách stupně po `step` sekundách, `samples` bodů od času
// `time`. Hodiny si polohu mezi body dopočítávají samy, takže se ptají jen
// jednou za minutu a obrazovka se přesto hýbe plynule.
//
//   {"v":1,"time":1789507185,"step":15,"samples":13,"sun":-334,"total":12,
//    "age":3,"sats":[{"id":25544,"n":"ISS (ZARYA)","g":0,"h":418,"r":1210,
//    "l":1,"p":[2750,153,2761,189,...]}],
//    "passes":[{"id":62713,"n":"SATGUS","rise":...,"set":...,"maxTime":...,
//    "max":31,"vis":1,"pref":1},{"id":25544,"n":"ISS",...}],
//    "pass":{"id":25544,...},"problem":"...","pending":["starlink"]}
//
// "passes" nese nejbližší přelet každé družice, na kterou se hlásí upozornění;
// "pass" je tentýž záznam ISS pro starší firmware, který seznam ještě nezná.

// Stejné stropy jako na serveru. Víc družic by se na kruh stejně nevešlo.
constexpr size_t SATELLITE_MAX_TRACKS = 150;
constexpr size_t SATELLITE_MAX_SAMPLES = 13;
constexpr size_t SATELLITE_NAME_LENGTH = 25;

// Skupiny v pořadí serveru. Index chodí v odpovědi jako "g" a v nastavení je to
// číslo bitu, takže se pořadí nesmí měnit.
enum SatelliteGroup : uint8_t {
  SATELLITE_GROUP_STATIONS = 0,
  SATELLITE_GROUP_VISUAL = 1,
  SATELLITE_GROUP_WEATHER = 2,
  SATELLITE_GROUP_GNSS = 3,
  SATELLITE_GROUP_AMATEUR = 4,
  SATELLITE_GROUP_STARLINK = 5,
  // Jediná družice, ne skupina CelesTraku: SATGUS (NORAD 62713) fotí nad Zemí
  // snímky nahrané z domova, takže má na obrazovce vlastní barvu i upozornění.
  SATELLITE_GROUP_SATGUS = 6,
  SATELLITE_GROUP_COUNT = 7,
};

// Jméno skupiny, jak ho čte server v parametru groups. Mimo rozsah nullptr.
const char *satelliteGroupName(uint8_t group);

struct SatelliteTrack {
  uint32_t noradId = 0;
  char name[SATELLITE_NAME_LENGTH] = "";
  uint8_t group = 0;
  // Na Slunci v čase `time`; ve stínu Země ji nikdo neuvidí ani za tmy.
  bool sunlit = false;
  uint16_t altitudeKm = 0;
  uint16_t rangeKm = 0;
  int16_t azimuthTenths[SATELLITE_MAX_SAMPLES] = {};
  int16_t elevationTenths[SATELLITE_MAX_SAMPLES] = {};
};

// Delší jméno server u přeletu neposílá; "SATGUS" je zatím nejdelší.
constexpr size_t SATELLITE_PASS_NAME_LENGTH = 13;
// Kolik přeletů najednou (ISS a SATGUS); víc by se na jeden řádek nevešlo.
constexpr size_t SATELLITE_MAX_PASSES = 2;

// Nejbližší přelet jedné družice. Časy jsou unixové sekundy.
struct SatellitePass {
  bool valid = false;
  char name[SATELLITE_PASS_NAME_LENGTH] = "";
  int64_t rise = 0;
  int64_t set = 0;
  int64_t maxTime = 0;
  uint8_t maxElevationDeg = 0;
  // Aspoň část přeletu je družice na Slunci a pozorovatel ve tmě.
  bool visible = false;
  // Domácí družice: když začínají skoro současně, píše se radši tahle.
  bool preferred = false;
};

struct SatelliteFeedInfo {
  int64_t startEpoch = 0;
  uint16_t stepSeconds = 0;
  uint8_t sampleCount = 0;
  bool hasSunElevation = false;
  float sunElevationDeg = 0.0f;
  // Družic nad nastavenou výškou před stropem serveru.
  uint16_t total = 0;
  uint16_t ageHours = 0;
  size_t count = 0;
  // Některou skupinu server ještě stahuje z CelesTraku.
  bool pending = false;
  SatellitePass passes[SATELLITE_MAX_PASSES];
  uint8_t passCount = 0;
  char problem[64] = "";
};

enum class SatelliteParseStatus : uint8_t {
  Ok,
  NotJson,
  // JSON, ale bez času nebo s tvarem, kterému tahle verze nerozumí.
  Invalid,
};

// Rozebere odpověď. Družice s nesmyslnými hodnotami se přeskočí, zbytek platí.
SatelliteParseStatus satelliteParseFeed(const char *begin, const char *end,
                                        SatelliteTrack *tracks, size_t capacity,
                                        SatelliteFeedInfo &info);

// Adresa dotazu: základní adresa z nastavení (klidně se jménem a heslem)
// doplněná o polohu, skupiny z masky bitů a nejmenší výšku. Vrací false, když se
// nevejde nebo maska nemá žádnou známou skupinu.
bool satelliteFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                           uint8_t groupMask, uint8_t minElevationDeg,
                           char *output, size_t capacity);

// Poloha na obloze v jednom okamžiku. x a y jsou v kruhu o poloměru 1: střed je
// zenit, okraj obzor, sever nahoře (y = -1) a východ vpravo (x = +1). Výška se
// interpoluje zvlášť, protože z tětivy mezi dvěma body na obzoru by vyšla vyšší.
struct SatelliteSkyPoint {
  float x = 0.0f;
  float y = 0.0f;
  float azimuthDeg = 0.0f;
  float elevationDeg = 0.0f;
};

void satelliteSkyProject(float azimuthDeg, float elevationDeg, float &x,
                         float &y);

// Poloha družice v čase epoch (unixové sekundy i se zlomkem). Poloha se
// interpoluje v rovině kruhu, ne v azimutu - přes sever by azimut skočil
// z 359 na 0 stupňů. Vrací false mimo okno dráhy.
bool satelliteTrackAt(const SatelliteTrack &track, const SatelliteFeedInfo &info,
                      double epoch, SatelliteSkyPoint &point);

// Konec okna dráhy v unixových sekundách.
double satelliteFeedEndEpoch(const SatelliteFeedInfo &info);

// Krátké jméno na popisek: bez závorky na konci, "ISS (ZARYA)" -> "ISS".
void satelliteShortName(const char *name, char *output, size_t capacity);

// Přelet na řádek pod oblohou: ten, který začíná nejdřív a ještě neskončil.
// Domácí družice vyhraje i tehdy, když začíná o něco později než druhá - jinak
// by ji ISS vytlačila skoro pokaždé. Vrací nullptr, když žádný nezbyl.
const SatellitePass *satellitePickPass(const SatelliteFeedInfo &info,
                                       int64_t now);

// O kolik smí domácí družice začínat později a přesto vyhrát.
constexpr int64_t SATELLITE_PASS_PREFER_SECONDS = 30 * 60;
