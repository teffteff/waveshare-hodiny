#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AdsbParser.h"

// Letadlo poblíž a výhled z okna. Když se letadlo přiblíží k domovu, hodiny
// samy přepnou na radar letadel a otevřou jeho detail. Rozhodování, které
// letadlo to má být, je tady - bez Arduina a FreeRTOS, aby šlo testovat na
// počítači. Stahování, zámky a přepínání obrazovek zůstávají ve službě
// a v hlavní smyčce. Podle MeteoPlaneRadar 0.8.0 (PlaneWatch, viz
// THIRD_PARTY_NOTICES.md).
//
// Pořadí:
//   1. letadlo ve VÝHLEDU Z OKNA - do dohledu okna a uvnitř jeho úhlu - i když
//      je dál než jiné;
//   2. jinak nejbližší v okruhu "poblíž".
// Výhled vyhrává nad vzdáleností, protože letadlo tři kilometry za domem je
// blíž než to devět kilometrů před oknem, ale vidět je jen to druhé.

// Kolik letadel si hodiny pamatují jako už ukázaná a jak dlouho.
constexpr size_t PLANE_NEARBY_MEMORY_SIZE = 16;
constexpr uint32_t PLANE_NEARBY_FORGET_MS = 15UL * 60UL * 1000UL;

constexpr uint8_t PLANE_WINDOW_DIRECTION_COUNT = 8;

struct PlaneNearbySettings {
  bool enabled = false;
  uint8_t radiusKm = 5;
  bool windowEnabled = false;
  // 0 = sever, po 45 stupních po směru hodinových ručiček (S, SV, V, ... SZ).
  uint8_t windowDirection = 0;
  uint8_t windowWidthDeg = 90;
  uint8_t windowRangeKm = 15;
};

// Filtr z nastavení radaru (výška a volací značka), aby to, co je schované na
// mapě, nevyskakovalo ani samo. Výškové meze platí jen s altitudeLimited;
// bez něj se výška neposuzuje.
struct PlaneFilter {
  bool onlyWithCallsign = false;
  bool altitudeLimited = false;
  uint16_t altitudeMinFt = 0;
  uint16_t altitudeMaxFt = 0;
};

// Táž podmínka, kterou radar kreslí mapu: neznámou výšku filtr nezahazuje.
bool planeFilterPasses(const AdsbAircraft &aircraft, const PlaneFilter &filter);

// Paměť ukázaných letadel: ICAO adresa a čas. Kruhová, takže při plné paměti
// vypadne nejstarší záznam.
struct PlaneNearbyMemory {
  struct Entry {
    char hex[8];
    uint32_t at;
  };
  Entry entries[PLANE_NEARBY_MEMORY_SIZE] = {};
  uint8_t next = 0;
};

bool planeNearbySeenRecently(const PlaneNearbyMemory &memory, const char *hex,
                             uint32_t now);
void planeNearbyRemember(PlaneNearbyMemory &memory, const char *hex,
                         uint32_t now);

// Vzdálenost (km) a azimut (stupně 0..360, 0 = sever) od domova k bodu. Na
// desítky kilometrů stačí rovinná aproximace, stejně jako u průletu.
void planeNearbyDistanceBearing(float homeLatitude, float homeLongitude,
                                float latitude, float longitude,
                                float &distanceKm, float &bearingDeg);

// Úhlový rozdíl dvou azimutů, 0..180 stupňů, přes přechod 360 -> 0.
float planeNearbyAngleDiff(float a, float b);

// Azimut středu okna ve stupních.
float planeNearbyWindowAzimuth(uint8_t direction);

// Leží bod (vzdálenost, azimut od domova) ve výhledu z okna?
bool planeNearbyInWindow(const PlaneNearbySettings &settings, float distanceKm,
                         float bearingDeg);

// Poloměr, na který se služba ptá se schovanou obrazovkou: okruh "poblíž",
// nebo dohled okna, sahá-li dál.
float planeNearbyWatchRadiusKm(const PlaneNearbySettings &settings);

// Nejmenší z nabízených dosahů radaru, do kterého se letadlo v dané
// vzdálenosti vejde i s rezervou na pohyb během podržení. Bez vhodného dosahu
// ten největší.
uint8_t planeNearbyRangeIndex(float distanceKm, const uint16_t *rangesKm,
                              size_t rangeCount);

struct PlaneNearbyPick {
  // Index do seznamu, -1 = nic.
  int index = -1;
  float distanceKm = 0.0f;
  float bearingDeg = 0.0f;
  bool inWindow = false;
};

// Vybere letadlo, které stojí za přepnutí, a přeskočí už ukázaná - jinak by
// jedno letadlo zaparkované v okně blokovalo všechna ostatní. Sondy a letadla
// bez výšky se nepočítají. Paměť se nemění: zapamatuje se až to, co se
// opravdu ukáže.
bool planeNearbyPick(const AdsbAircraft *list, size_t count,
                     float homeLatitude, float homeLongitude,
                     const PlaneNearbySettings &settings,
                     const PlaneFilter &filter,
                     const PlaneNearbyMemory &memory, uint32_t now,
                     PlaneNearbyPick &pick);
