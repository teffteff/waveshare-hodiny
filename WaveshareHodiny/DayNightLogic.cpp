#include "DayNightLogic.h"

#include "Astronomy.h"

namespace {
constexpr int64_t VALID_TIME_THRESHOLD = 1700000000LL;
constexpr int64_t MAX_LAST_CHANGED_DRIFT_SECONDS = 5 * 60;
}

bool clockSelectCompletedTransitionTimestamp(
    bool lastChangedAvailable, int64_t lastChangedTimestamp,
    bool expectedTransitionAvailable, int64_t expectedTransitionTimestamp,
    int64_t &selectedTransitionTimestamp) {
  if (!expectedTransitionAvailable) return false;
  selectedTransitionTimestamp = expectedTransitionTimestamp;
  if (!lastChangedAvailable) return true;
  const int64_t difference =
      lastChangedTimestamp >= expectedTransitionTimestamp
          ? lastChangedTimestamp - expectedTransitionTimestamp
          : expectedTransitionTimestamp - lastChangedTimestamp;
  if (difference <= MAX_LAST_CHANGED_DRIFT_SECONDS) {
    selectedTransitionTimestamp = lastChangedTimestamp;
  }
  return true;
}

ClockSunDecision clockEvaluateSunDecision(
    bool horizonIsDay, int8_t sunriseOffsetMinutes,
    int8_t sunsetOffsetMinutes, int64_t nowTimestamp,
    bool completedTransitionAvailable, int64_t completedTransitionTimestamp,
    bool nextTransitionAvailable, int64_t nextTransitionTimestamp) {
  const int8_t completedOffset =
      horizonIsDay ? sunriseOffsetMinutes : sunsetOffsetMinutes;
  const int8_t upcomingOffset =
      horizonIsDay ? sunsetOffsetMinutes : sunriseOffsetMinutes;
  const bool needsCurrentTime = completedOffset > 0 || upcomingOffset < 0;
  if (needsCurrentTime && nowTimestamp < VALID_TIME_THRESHOLD) {
    return ClockSunDecision::Unavailable;
  }
  if ((completedOffset > 0 && !completedTransitionAvailable) ||
      (upcomingOffset < 0 && !nextTransitionAvailable)) {
    return ClockSunDecision::Unavailable;
  }

  bool isDay = horizonIsDay;
  if (completedOffset > 0 && completedTransitionAvailable &&
      nowTimestamp < completedTransitionTimestamp + completedOffset * 60LL) {
    isDay = !isDay;
  } else if (upcomingOffset < 0 && nextTransitionAvailable &&
             nowTimestamp >=
                 nextTransitionTimestamp + upcomingOffset * 60LL) {
    isDay = !isDay;
  }
  return isDay ? ClockSunDecision::Day : ClockSunDecision::Night;
}

ClockLocalSunState clockLocalSunState(int64_t now, int64_t dayStart,
                                      int64_t dayLength, double latitudeDeg,
                                      double longitudeDeg,
                                      int8_t sunriseOffsetMinutes,
                                      int8_t sunsetOffsetMinutes) {
  constexpr int64_t LOOKBACK_SECONDS = 36 * 60 * 60;
  ClockLocalSunState state;
  int64_t event = 0;
  if (astronomyFindEvent(AstronomyBody::Sun, true, dayStart, dayLength,
                         latitudeDeg, longitudeDeg, event))
    state.sunrise = event;
  if (astronomyFindEvent(AstronomyBody::Sun, false, dayStart, dayLength,
                         latitudeDeg, longitudeDeg, event))
    state.sunset = event;

  // Poslední posunutá hranice před teď. Hledá se od now minus posun, takže
  // východ s posunem +30 min se započítá až půl hodiny po skutečném východu.
  int64_t lastRise = 0;
  int64_t lastSet = 0;
  const bool haveRise = astronomyFindEvent(
      AstronomyBody::Sun, true, now - sunriseOffsetMinutes * 60LL,
      -LOOKBACK_SECONDS, latitudeDeg, longitudeDeg, lastRise);
  const bool haveSet = astronomyFindEvent(
      AstronomyBody::Sun, false, now - sunsetOffsetMinutes * 60LL,
      -LOOKBACK_SECONDS, latitudeDeg, longitudeDeg, lastSet);
  if (haveRise && haveSet) {
    state.isDay = lastRise + sunriseOffsetMinutes * 60LL >
                  lastSet + sunsetOffsetMinutes * 60LL;
  } else if (haveRise != haveSet) {
    state.isDay = haveRise;
  } else {
    state.isDay = astronomyHorizonMarginDeg(AstronomyBody::Sun, now,
                                            latitudeDeg, longitudeDeg) >= 0.0;
  }
  return state;
}
