#include "ScreenSchedule.h"

#include <time.h>

#include "Astronomy.h"

namespace {

constexpr int64_t SECONDS_PER_DAY = 86400;

// Místní půlnoc dne, do kterého patří `moment`, posunutá o `days` dní.
// Přes mktime, aby den s přechodem na letní čas měl svých 23 nebo 25 hodin.
int64_t localMidnight(int64_t moment, int days) {
  const time_t seconds = static_cast<time_t>(moment);
  struct tm local;
  localtime_r(&seconds, &local);
  local.tm_mday += days;
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&local));
}

bool sameEvent(uint8_t leftEvent, int16_t leftValue, uint8_t rightEvent,
               int16_t rightValue) {
  return leftEvent == rightEvent && leftValue == rightValue;
}

}  // namespace

bool screenScheduleEventOnDay(uint8_t event, int16_t value,
                              int64_t localMidnight, double latitudeDeg,
                              double longitudeDeg, int64_t &at) {
  if (event == CLOCK_SCHEDULE_TIME) {
    const time_t seconds = static_cast<time_t>(localMidnight);
    struct tm local;
    localtime_r(&seconds, &local);
    local.tm_hour = value / 60;
    local.tm_min = value % 60;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    at = static_cast<int64_t>(mktime(&local));
    return true;
  }
  int64_t found = 0;
  bool ok = false;
  switch (event) {
    case CLOCK_SCHEDULE_SUNRISE:
    case CLOCK_SCHEDULE_SUNSET:
      ok = astronomyFindEvent(AstronomyBody::Sun,
                              event == CLOCK_SCHEDULE_SUNRISE, localMidnight,
                              SECONDS_PER_DAY, latitudeDeg, longitudeDeg,
                              found);
      break;
    case CLOCK_SCHEDULE_CIVIL_DAWN:
    case CLOCK_SCHEDULE_CIVIL_DUSK:
      ok = astronomyFindSunAltitude(ASTRONOMY_CIVIL_TWILIGHT_DEG,
                                    event == CLOCK_SCHEDULE_CIVIL_DAWN,
                                    localMidnight, SECONDS_PER_DAY,
                                    latitudeDeg, longitudeDeg, found);
      break;
    default:
      return false;
  }
  if (!ok) return false;
  at = found + static_cast<int64_t>(value) * 60;
  return true;
}

int screenScheduleActiveRule(const ClockScreenScheduleConfig &schedule,
                             int64_t now, double latitudeDeg,
                             double longitudeDeg, int64_t *until) {
  for (int index = 0; index < static_cast<int>(CLOCK_SCREEN_SCHEDULE_COUNT);
       ++index) {
    const ClockScreenScheduleRule &rule = schedule.rules[index];
    if (rule.screen >= CLOCK_SCREEN_ORDER_COUNT) continue;
    // Stejný začátek i konec by bylo okno bez délky, nebo na celý den; obojí
    // je spíš nedovyplněný formulář než úmysl.
    if (sameEvent(rule.startEvent, rule.startValue, rule.endEvent,
                  rule.endValue))
      continue;

    // Poslední začátek, který už nastal: dnes, jinak včera. S posunem
    // o hodiny může dnešní událost ležet i za dnešní půlnocí, proto se
    // zkouší i zítřek, který v tom případě začal ještě dnes.
    int64_t start = 0;
    bool haveStart = false;
    for (int day = 1; day >= -2 && !haveStart; --day) {
      int64_t candidate = 0;
      if (screenScheduleEventOnDay(rule.startEvent, rule.startValue,
                                   localMidnight(now, day), latitudeDeg,
                                   longitudeDeg, candidate) &&
          candidate <= now) {
        start = candidate;
        haveStart = true;
      }
    }
    if (!haveStart) continue;

    // První konec po začátku: ve dni začátku, jinak ve dni po něm.
    int64_t end = 0;
    bool haveEnd = false;
    for (int day = -1; day <= 2 && !haveEnd; ++day) {
      int64_t candidate = 0;
      if (screenScheduleEventOnDay(rule.endEvent, rule.endValue,
                                   localMidnight(start, day), latitudeDeg,
                                   longitudeDeg, candidate) &&
          candidate > start) {
        end = candidate;
        haveEnd = true;
      }
    }
    if (!haveEnd || now >= end) continue;
    if (until != nullptr) *until = end;
    return index;
  }
  return -1;
}
