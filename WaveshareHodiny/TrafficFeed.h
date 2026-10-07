#pragma once

#include <stddef.h>
#include <stdint.h>

// Obrazovka Doprava: rozbor odpovědi serveru (toulky /api/doprava?for=hodiny).
// Server všechno spočítá i promítne do pixelů displeje 480x480 (vše leží do
// poloměru 215 kolem středu), takže hodiny jen kreslí. Rozbor nestojí na
// Arduinu, aby šel testovat na počítači.
//
// Tvar odpovědi:
//   {"now": unix, "t": unix nebo null, "stale": bool,
//    "lines": [{"id": "ondrejov-praha", "xy": [x0,y0,x1,y1,...],
//               "segs": [[od, do, úroveň], ...]}],
//    "places": [{"name": "Benešov", "xy": [x,y]}],
//    "exits": [{"n": 15, "xy": [x,y]}],
//    "buses": [{"line": "383", "xy": [x,y], "delay": sekundy nebo null}],
//    "drives": [{"to": "Praha", "min": 30, "usual": 29, "back": 30}],
//    "texts": [...], "warnings": [...], "departures": [...]}

constexpr uint8_t TRAFFIC_MAX_LINES = 8;
constexpr uint8_t TRAFFIC_MAX_POINTS = 120;
constexpr uint8_t TRAFFIC_MAX_SEGMENTS = 16;
constexpr uint8_t TRAFFIC_MAX_PLACES = 8;
constexpr uint8_t TRAFFIC_MAX_EXITS = 10;
constexpr uint8_t TRAFFIC_MAX_BUSES = 24;
constexpr uint8_t TRAFFIC_MAX_DRIVES = 4;
constexpr uint8_t TRAFFIC_MAX_TEXTS = 6;
constexpr uint8_t TRAFFIC_MAX_WARNINGS = 4;
constexpr uint8_t TRAFFIC_MAX_DEPARTURES = 4;
// Texty v UTF-8 i s ukončovací nulou. Čeština bere na znak až dva bajty.
constexpr size_t TRAFFIC_TEXT_LENGTH = 96;
constexpr size_t TRAFFIC_NAME_LENGTH = 32;
constexpr size_t TRAFFIC_BUS_LINE_LENGTH = 8;
constexpr size_t TRAFFIC_LINE_ID_LENGTH = 24;

// Úroveň provozu na úseku. Stejné hranice jako na serveru.
enum TrafficLevel : uint8_t {
  TRAFFIC_LEVEL_FREE = 0,
  TRAFFIC_LEVEL_SLOW = 1,
  // Kolona: 40 km/h a pomaleji.
  TRAFFIC_LEVEL_JAM = 2,
  // Stojí: 15 km/h a pomaleji.
  TRAFFIC_LEVEL_STANDING = 3,
  TRAFFIC_LEVEL_CLOSED = 4,
};

// Co čára znamená. Podle toho se kreslí: D1 oběma směry vedle sebe, objížďka
// přes Ondřejov tenčí.
enum class TrafficLineKind : uint8_t {
  Drive,     // cesta z domova (ondrejov-praha, ondrejov-benesov)
  D1Prague,  // d1-P
  D1Brno,    // d1-B
  Via,       // via-B, II/113 přes Ondřejov
  Other,
};

struct TrafficSegment {
  // Indexy bodů čáry, from < to.
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t level = TRAFFIC_LEVEL_FREE;
};

struct TrafficLine {
  TrafficLineKind kind = TrafficLineKind::Other;
  char id[TRAFFIC_LINE_ID_LENGTH] = "";
  uint8_t pointCount = 0;
  int16_t x[TRAFFIC_MAX_POINTS] = {};
  int16_t y[TRAFFIC_MAX_POINTS] = {};
  uint8_t segmentCount = 0;
  TrafficSegment segments[TRAFFIC_MAX_SEGMENTS];
};

struct TrafficPlace {
  char name[TRAFFIC_NAME_LENGTH] = "";
  int16_t x = 0;
  int16_t y = 0;
};

struct TrafficExit {
  uint16_t number = 0;
  int16_t x = 0;
  int16_t y = 0;
};

struct TrafficBus {
  char line[TRAFFIC_BUS_LINE_LENGTH] = "";
  int16_t x = 0;
  int16_t y = 0;
  bool hasDelay = false;
  int32_t delaySeconds = 0;
};

struct TrafficDrive {
  char to[TRAFFIC_NAME_LENGTH] = "";
  // Minuty; -1, když server údaj nemá.
  int16_t minutes = -1;
  int16_t usualMinutes = -1;
  int16_t backMinutes = -1;
  // Důvod zdržení z textu cesty ("stojí Chodov - Tomíčkova, 12 km/h"),
  // jinak prázdné.
  char detail[TRAFFIC_TEXT_LENGTH] = "";
};

struct TrafficData {
  int64_t now = 0;
  // Čas vzorku provozu; 0, když ho server nemá.
  int64_t sampleTime = 0;
  // Provoz je starší než 25 minut.
  bool stale = false;
  uint8_t lineCount = 0;
  TrafficLine lines[TRAFFIC_MAX_LINES];
  uint8_t placeCount = 0;
  TrafficPlace places[TRAFFIC_MAX_PLACES];
  uint8_t exitCount = 0;
  TrafficExit exits[TRAFFIC_MAX_EXITS];
  uint8_t busCount = 0;
  TrafficBus buses[TRAFFIC_MAX_BUSES];
  uint8_t driveCount = 0;
  TrafficDrive drives[TRAFFIC_MAX_DRIVES];
  // Texty, které nepatří k žádné cestě - tedy D1 oběma směry. Text cesty se
  // rozloží do TrafficDrive.detail.
  uint8_t textCount = 0;
  char texts[TRAFFIC_MAX_TEXTS][TRAFFIC_TEXT_LENGTH] = {};
  uint8_t warningCount = 0;
  char warnings[TRAFFIC_MAX_WARNINGS][TRAFFIC_TEXT_LENGTH] = {};
  uint8_t departureCount = 0;
  char departures[TRAFFIC_MAX_DEPARTURES][TRAFFIC_TEXT_LENGTH] = {};
};

enum class TrafficParseStatus : uint8_t { Ok, NotJson, Invalid };

// Rozebere odpověď do `data`. Při jiném výsledku než Ok obsah `data`
// nedefinovaný - volající rozebírá do pracovní kopie. Přebytečné položky nad
// stropy se zahodí, rozbitá položka se přeskočí; neplatný je jen dokument
// bez objektu nebo bez pole "lines".
TrafficParseStatus trafficFeedParse(const char *json, size_t length,
                                    TrafficData &data);

// Převede interpunkci, kterou písmo hodin nemá (·, →, –, …, nezlomitelná
// mezera), na znaky, které má. Mění text na místě; výsledek nikdy není delší.
void trafficFoldText(char *text);
