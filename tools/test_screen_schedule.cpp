#include <cassert>
#include <cstdlib>
#include <ctime>

#include "ScreenSchedule.h"

namespace {

constexpr double LATITUDE = 49.90461;
constexpr double LONGITUDE = 14.7842;

// Místní čas v Praze jako unixové sekundy.
int64_t prague(int year, int month, int day, int hour, int minute) {
  struct tm local = {};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&local));
}

int localMinute(int64_t moment) {
  const time_t seconds = static_cast<time_t>(moment);
  struct tm local;
  localtime_r(&seconds, &local);
  return local.tm_hour * 60 + local.tm_min;
}

ClockScreenScheduleRule rule(uint8_t screen, uint8_t startEvent,
                             int16_t startValue, uint8_t endEvent,
                             int16_t endValue) {
  ClockScreenScheduleRule result;
  result.screen = screen;
  result.startEvent = startEvent;
  result.startValue = startValue;
  result.endEvent = endEvent;
  result.endValue = endValue;
  return result;
}

int active(const ClockScreenScheduleConfig &schedule, int64_t now,
           int64_t *until = nullptr) {
  return screenScheduleActiveRule(schedule, now, LATITUDE, LONGITUDE, until);
}

void testEvents() {
  const int64_t midnight = prague(2026, 9, 19, 0, 0);
  int64_t at = 0;
  assert(screenScheduleEventOnDay(CLOCK_SCHEDULE_TIME, 7 * 60 + 30, midnight,
                                  LATITUDE, LONGITUDE, at));
  assert(at == prague(2026, 9, 19, 7, 30));
  // 19. září v Ondřejově: východ kolem 6:48, západ kolem 19:08, občanské
  // svítání a soumrak zhruba o půl hodiny dřív a později.
  assert(screenScheduleEventOnDay(CLOCK_SCHEDULE_SUNRISE, 0, midnight,
                                  LATITUDE, LONGITUDE, at));
  const int sunrise = localMinute(at);
  assert(sunrise > 6 * 60 + 35 && sunrise < 7 * 60);
  assert(screenScheduleEventOnDay(CLOCK_SCHEDULE_SUNSET, 0, midnight, LATITUDE,
                                  LONGITUDE, at));
  const int sunset = localMinute(at);
  assert(sunset > 18 * 60 + 55 && sunset < 19 * 60 + 20);
  assert(screenScheduleEventOnDay(CLOCK_SCHEDULE_CIVIL_DAWN, 0, midnight,
                                  LATITUDE, LONGITUDE, at));
  const int dawn = localMinute(at);
  assert(dawn < sunrise && dawn > sunrise - 45);
  assert(screenScheduleEventOnDay(CLOCK_SCHEDULE_CIVIL_DUSK, 0, midnight,
                                  LATITUDE, LONGITUDE, at));
  const int dusk = localMinute(at);
  assert(dusk > sunset && dusk < sunset + 45);
  // Posun v minutách.
  int64_t shifted = 0;
  assert(screenScheduleEventOnDay(CLOCK_SCHEDULE_CIVIL_DUSK, -20, midnight,
                                  LATITUDE, LONGITUDE, shifted));
  assert(shifted == at - 20 * 60);
  // Za polárním kruhem v prosinci Slunce nevyjde.
  assert(!screenScheduleEventOnDay(CLOCK_SCHEDULE_SUNRISE, 0,
                                   prague(2026, 12, 21, 0, 0), 78.2, 15.6, at));
}

void testTwilightExample() {
  // Družice od občanského soumraku do svítání, letadla přes den.
  ClockScreenScheduleConfig schedule;
  schedule.rules[0] = rule(CLOCK_SCREEN_SATELLITES, CLOCK_SCHEDULE_CIVIL_DUSK,
                           0, CLOCK_SCHEDULE_CIVIL_DAWN, 0);
  schedule.rules[1] = rule(CLOCK_SCREEN_PLANES, CLOCK_SCHEDULE_CIVIL_DAWN, 0,
                           CLOCK_SCHEDULE_CIVIL_DUSK, 0);
  int64_t until = 0;
  assert(active(schedule, prague(2026, 9, 19, 12, 0), &until) == 1);
  assert(localMinute(until) > 19 * 60);
  assert(active(schedule, prague(2026, 9, 19, 23, 0), &until) == 0);
  // Okno přes půlnoc platí i po ní a končí ranním svítáním.
  assert(active(schedule, prague(2026, 9, 20, 2, 0), &until) == 0);
  assert(localMinute(until) > 6 * 60 && localMinute(until) < 7 * 60);
  // Hodinu před svítáním pořád noc, po něm den.
  assert(active(schedule, prague(2026, 9, 20, 5, 0)) == 0);
  assert(active(schedule, prague(2026, 9, 20, 7, 0)) == 1);
}

void testFixedWindows() {
  ClockScreenScheduleConfig schedule;
  schedule.rules[0] =
      rule(CLOCK_SCREEN_SCHOOL, CLOCK_SCHEDULE_TIME, 6 * 60 + 30,
           CLOCK_SCHEDULE_TIME, 7 * 60 + 45);
  assert(active(schedule, prague(2026, 9, 21, 6, 29)) == -1);
  assert(active(schedule, prague(2026, 9, 21, 6, 30)) == 0);
  assert(active(schedule, prague(2026, 9, 21, 7, 44)) == 0);
  assert(active(schedule, prague(2026, 9, 21, 7, 45)) == -1);
  // Přes půlnoc: 22:00-5:00.
  schedule.rules[0] = rule(CLOCK_SCREEN_CLOCK, CLOCK_SCHEDULE_TIME, 22 * 60,
                           CLOCK_SCHEDULE_TIME, 5 * 60);
  assert(active(schedule, prague(2026, 9, 21, 21, 59)) == -1);
  assert(active(schedule, prague(2026, 9, 21, 23, 30)) == 0);
  assert(active(schedule, prague(2026, 9, 22, 4, 59)) == 0);
  assert(active(schedule, prague(2026, 9, 22, 5, 0)) == -1);
  // Přechod na zimní čas (25. 10. 2026): noc má 25 hodin, okno drží.
  assert(active(schedule, prague(2026, 10, 25, 2, 30)) == 0);
  assert(active(schedule, prague(2026, 10, 25, 6, 0)) == -1);
  // Stejný začátek i konec je nedovyplněný formulář, ne celý den.
  schedule.rules[0] = rule(CLOCK_SCREEN_CLOCK, CLOCK_SCHEDULE_TIME, 600,
                           CLOCK_SCHEDULE_TIME, 600);
  assert(active(schedule, prague(2026, 9, 21, 10, 0)) == -1);
}

void testPriorityAndDisabledRules() {
  ClockScreenScheduleConfig schedule;
  schedule.rules[0] = rule(CLOCK_SCREEN_ORDER_UNUSED, CLOCK_SCHEDULE_TIME, 0,
                           CLOCK_SCHEDULE_TIME, 1439);
  schedule.rules[1] = rule(CLOCK_SCREEN_RADAR, CLOCK_SCHEDULE_TIME, 8 * 60,
                           CLOCK_SCHEDULE_TIME, 9 * 60);
  schedule.rules[2] = rule(CLOCK_SCREEN_RSS, CLOCK_SCHEDULE_TIME, 7 * 60,
                           CLOCK_SCHEDULE_TIME, 12 * 60);
  // Vypnuté pravidlo se přeskočí, z překrývajících se vyhrává vyšší.
  assert(active(schedule, prague(2026, 9, 21, 8, 30)) == 1);
  assert(active(schedule, prague(2026, 9, 21, 10, 0)) == 2);
  assert(active(schedule, prague(2026, 9, 21, 13, 0)) == -1);
}

void testOffsetsAroundSunset() {
  // Hodinu před západem až dvě hodiny po něm.
  ClockScreenScheduleConfig schedule;
  schedule.rules[0] = rule(CLOCK_SCREEN_SKY, CLOCK_SCHEDULE_SUNSET, -60,
                           CLOCK_SCHEDULE_SUNSET, 120);
  const int64_t midnight = prague(2026, 9, 19, 0, 0);
  int64_t sunset = 0;
  assert(screenScheduleEventOnDay(CLOCK_SCHEDULE_SUNSET, 0, midnight, LATITUDE,
                                  LONGITUDE, sunset));
  assert(active(schedule, sunset - 61 * 60) == -1);
  assert(active(schedule, sunset - 59 * 60) == 0);
  assert(active(schedule, sunset + 119 * 60) == 0);
  assert(active(schedule, sunset + 121 * 60) == -1);
}

}  // namespace

int main() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  testEvents();
  testTwilightExample();
  testFixedWindows();
  testPriorityAndDisabledRules();
  testOffsetsAroundSunset();
  return 0;
}
