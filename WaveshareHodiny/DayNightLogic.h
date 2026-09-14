#pragma once

#include <stdint.h>

enum class ClockSunDecision : uint8_t {
  Unavailable = 0,
  Day,
  Night,
};

bool clockSelectCompletedTransitionTimestamp(
    bool lastChangedAvailable, int64_t lastChangedTimestamp,
    bool expectedTransitionAvailable, int64_t expectedTransitionTimestamp,
    int64_t &selectedTransitionTimestamp);

ClockSunDecision clockEvaluateSunDecision(
    bool horizonIsDay, int8_t sunriseOffsetMinutes,
    int8_t sunsetOffsetMinutes, int64_t nowTimestamp,
    bool completedTransitionAvailable, int64_t completedTransitionTimestamp,
    bool nextTransitionAvailable, int64_t nextTransitionTimestamp);

// Den a noc podle Slunce spočítaného na zařízení (Astronomy.h), pro zdroj
// Open-Meteo. Dřív se o nich rozhodovalo jen při stažení počasí po deseti
// minutách: přepnutí přišlo až o tolik později a při výpadku Open-Meteo zamrzlo.
//
// Den začíná východem posunutým o sunriseOffsetMinutes a končí západem
// posunutým o sunsetOffsetMinutes; rozhoduje, která z těch dvou hranic byla
// naposledy. Za polárním kruhem, kde žádná nenastane, rozhoduje výška Slunce.
struct ClockLocalSunState {
  bool isDay = true;
  // Dnešní východ a západ bez posunu, jak je ukazuje web. 0 = dnes nenastane.
  int64_t sunrise = 0;
  int64_t sunset = 0;
};

// dayStart je dnešní místní půlnoc a dayLength délka dnešního dne v sekundách
// (při změně času 23 nebo 25 hodin).
ClockLocalSunState clockLocalSunState(int64_t now, int64_t dayStart,
                                      int64_t dayLength, double latitudeDeg,
                                      double longitudeDeg,
                                      int8_t sunriseOffsetMinutes,
                                      int8_t sunsetOffsetMinutes);
