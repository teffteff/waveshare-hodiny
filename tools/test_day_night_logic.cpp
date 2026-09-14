#include <cassert>
#include <cstdint>

#include "../WaveshareHodiny/Astronomy.h"
#include "../WaveshareHodiny/DayNightLogic.h"

namespace {
constexpr int64_t HOUR = 60 * 60;
constexpr int64_t VALID_NOW = 1760000000;
}

int main() {
  int64_t selectedTransition = 0;
  const int64_t expectedSunrise = VALID_NOW - 3 * HOUR;
  assert(clockSelectCompletedTransitionTimestamp(
      true, VALID_NOW - 15 * 60, true, expectedSunrise,
      selectedTransition));
  assert(selectedTransition == expectedSunrise);
  assert(clockEvaluateSunDecision(true, 30, 0, VALID_NOW, true,
                                  selectedTransition, true,
                                  VALID_NOW + 10 * HOUR) ==
         ClockSunDecision::Day);

  const int64_t expectedSunset = VALID_NOW - 4 * HOUR;
  assert(clockSelectCompletedTransitionTimestamp(
      true, VALID_NOW - 10 * 60, true, expectedSunset,
      selectedTransition));
  assert(selectedTransition == expectedSunset);
  assert(clockEvaluateSunDecision(false, 0, 60, VALID_NOW, true,
                                  selectedTransition, true,
                                  VALID_NOW + 8 * HOUR) ==
         ClockSunDecision::Night);

  assert(clockSelectCompletedTransitionTimestamp(
      true, expectedSunrise + 2 * 60, true, expectedSunrise,
      selectedTransition));
  assert(selectedTransition == expectedSunrise + 2 * 60);
  assert(clockSelectCompletedTransitionTimestamp(
      true, expectedSunrise + 6 * 60, true, expectedSunrise,
      selectedTransition));
  assert(selectedTransition == expectedSunrise);
  assert(clockSelectCompletedTransitionTimestamp(
      true, expectedSunrise + 2 * 60, true, expectedSunrise,
      selectedTransition));
  assert(clockEvaluateSunDecision(true, 30, 0,
                                  expectedSunrise + 20 * 60, true,
                                  selectedTransition, true,
                                  VALID_NOW + 10 * HOUR) ==
         ClockSunDecision::Night);
  assert(clockEvaluateSunDecision(true, 30, 0,
                                  expectedSunrise + 35 * 60, true,
                                  selectedTransition, true,
                                  VALID_NOW + 10 * HOUR) ==
         ClockSunDecision::Day);

  assert(!clockSelectCompletedTransitionTimestamp(
      true, VALID_NOW, false, 0, selectedTransition));

  assert(clockEvaluateSunDecision(false, 0, 60, 0, true,
                                  VALID_NOW - 2 * HOUR, true,
                                  VALID_NOW + 10 * HOUR) ==
         ClockSunDecision::Unavailable);

  assert(clockEvaluateSunDecision(false, 0, 60, VALID_NOW, true,
                                  VALID_NOW - 30 * 60, true,
                                  VALID_NOW + 10 * HOUR) ==
         ClockSunDecision::Day);
  assert(clockEvaluateSunDecision(false, 0, 60, VALID_NOW, true,
                                  VALID_NOW - 90 * 60, true,
                                  VALID_NOW + 10 * HOUR) ==
         ClockSunDecision::Night);
  assert(clockEvaluateSunDecision(false, 0, 60, VALID_NOW, false, 0, true,
                                  VALID_NOW + 10 * HOUR) ==
         ClockSunDecision::Unavailable);

  assert(clockEvaluateSunDecision(false, 0, 0, 0, false, 0, false, 0) ==
         ClockSunDecision::Night);
  assert(clockEvaluateSunDecision(true, 0, 0, 0, false, 0, false, 0) ==
         ClockSunDecision::Day);

  assert(clockEvaluateSunDecision(true, 0, -30, VALID_NOW, true,
                                  VALID_NOW - 10 * HOUR, true,
                                  VALID_NOW + 40 * 60) ==
         ClockSunDecision::Day);
  assert(clockEvaluateSunDecision(true, 0, -30, VALID_NOW, true,
                                  VALID_NOW - 10 * HOUR, true,
                                  VALID_NOW + 10 * 60) ==
         ClockSunDecision::Night);

  // Den a noc ze Slunce spočítaného na zařízení. Ondřejov, 14. 9. 2026; místní
  // půlnoc SELČ je 22:00 UTC předchozího dne.
  constexpr double LAT = 49.90461;
  constexpr double LON = 14.7842;
  constexpr int64_t DAY_START = 1789336800LL;
  constexpr int64_t DAY = 24 * HOUR;
  int64_t sunrise = 0;
  int64_t sunset = 0;
  assert(astronomyFindEvent(AstronomyBody::Sun, true, DAY_START, DAY, LAT, LON,
                            sunrise));
  assert(astronomyFindEvent(AstronomyBody::Sun, false, DAY_START, DAY, LAT, LON,
                            sunset));
  const auto isDay = [&](int64_t now, int8_t riseOffset, int8_t setOffset) {
    return clockLocalSunState(now, DAY_START, DAY, LAT, LON, riseOffset,
                              setOffset)
        .isDay;
  };
  const ClockLocalSunState today =
      clockLocalSunState(sunrise + HOUR, DAY_START, DAY, LAT, LON, 0, 0);
  assert(today.sunrise == sunrise && today.sunset == sunset);
  assert(!isDay(sunrise - 60, 0, 0));
  assert(isDay(sunrise + 60, 0, 0));
  assert(isDay(sunset - 60, 0, 0));
  assert(!isDay(sunset + 60, 0, 0));
  assert(!isDay(DAY_START + 60, 0, 0));
  // Posun východu o půl hodiny později a západu o půl hodiny dřív.
  assert(!isDay(sunrise + 20 * 60, 30, 0));
  assert(isDay(sunrise + 31 * 60, 30, 0));
  assert(isDay(sunset - 31 * 60, 0, -30));
  assert(!isDay(sunset - 29 * 60, 0, -30));
  // Záporný posun východu: den začne ještě před svítáním.
  assert(isDay(sunrise - 10 * 60, -15, 0));
  // Tromsø: v červnu polární den, v prosinci polární noc.
  assert(clockLocalSunState(1782043200LL, 1782000000LL - 2 * HOUR, DAY, 69.6492,
                            18.9553, 0, 0)
             .isDay);
  const ClockLocalSunState polarNight = clockLocalSunState(
      1797854400LL, 1797811200LL - HOUR, DAY, 69.6492, 18.9553, 0, 0);
  assert(!polarNight.isDay && polarNight.sunrise == 0 && polarNight.sunset == 0);
  return 0;
}
