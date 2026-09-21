#include "WeatherWarnings.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "JsonScan.h"

namespace {

// Unixový čas z odpovědi. Do floatu by se nevešel bez ztráty sekund, proto se
// čte po číslicích, stejně jako v RainAlert.
bool readEpoch(const JsonValue &value, int64_t &out) {
  if (!value.valid() || value.isString) return false;
  const char *cursor = value.contentBegin();
  const char *end = value.contentEnd();
  int64_t result = 0;
  bool digits = false;
  for (; cursor < end && *cursor >= '0' && *cursor <= '9'; ++cursor) {
    result = result * 10 + (*cursor - '0');
    if (result > 0xFFFFFFFFLL) return false;
    digits = true;
  }
  if (!digits || cursor != end) return false;
  out = result;
  return true;
}

bool readSmall(const char *begin, const char *end, const char *key,
               uint8_t &out) {
  float number = 0.0f;
  if (!jsonReadNumberMember(begin, end, key, number) || !std::isfinite(number))
    return false;
  const long rounded = lroundf(number);
  if (rounded < 0 || rounded > 255) return false;
  out = static_cast<uint8_t>(rounded);
  return true;
}

// Čas události vůči dnešku: "18:00", "zítra 18:00", "3. 10. 18:00".
void formatMoment(int64_t moment, int64_t now, bool english, char *output,
                  size_t capacity) {
  const time_t momentTime = static_cast<time_t>(moment);
  const time_t nowTime = static_cast<time_t>(now);
  struct tm local;
  struct tm today;
  localtime_r(&momentTime, &local);
  localtime_r(&nowTime, &today);
  // Půlnoc na konci dne se píše jako 24:00 dne předchozího; výstrahy ČHMÚ
  // končí o půlnoci skoro vždycky a "do zítra 00:00" by mátlo.
  int hour = local.tm_hour;
  int minute = local.tm_min;
  if (hour == 0 && minute == 0) {
    const time_t before = momentTime - 1;
    localtime_r(&before, &local);
    hour = 24;
  }
  struct tm tomorrow = today;
  tomorrow.tm_mday += 1;
  tomorrow.tm_hour = 12;
  const time_t tomorrowTime = mktime(&tomorrow);
  localtime_r(&tomorrowTime, &tomorrow);
  const bool sameDay =
      local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;
  const bool nextDay =
      local.tm_year == tomorrow.tm_year && local.tm_yday == tomorrow.tm_yday;
  if (sameDay) {
    snprintf(output, capacity, "%02d:%02d", hour, minute);
  } else if (nextDay) {
    snprintf(output, capacity, "%s %02d:%02d", english ? "tmrw" : "zítra", hour,
             minute);
  } else if (english) {
    static const char *const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug",
                                           "Sep", "Oct", "Nov", "Dec"};
    snprintf(output, capacity, "%s %d %02d:%02d", MONTHS[local.tm_mon % 12],
             local.tm_mday, hour, minute);
  } else {
    snprintf(output, capacity, "%d. %d. %02d:%02d", local.tm_mday,
             local.tm_mon + 1, hour, minute);
  }
}

}  // namespace

bool weatherWarningsParse(const char *begin, const char *end,
                          WeatherWarningFeed &feed) {
  feed = WeatherWarningFeed{};
  if (begin == nullptr || end == nullptr || begin >= end) return false;
  const char *objectBegin = jsonSkipWhitespace(begin, end);
  if (objectBegin >= end || *objectBegin != '{') return false;
  const char *objectEnd = jsonValueEnd(objectBegin, end);
  if (objectEnd == nullptr) return false;

  // Seznam musí dorazit, i prázdný: bez něj se nedá rozlišit klid od chyby.
  const JsonValue list = jsonFindMember(objectBegin, objectEnd, "warnings");
  if (!list.isArray()) return false;

  feed.covered = jsonReadBoolMember(objectBegin, objectEnd, "covered");
  feed.stale = jsonReadBoolMember(objectBegin, objectEnd, "stale");
  jsonCopyTextMember(objectBegin, objectEnd, "area", feed.area,
                     sizeof(feed.area));
  int64_t serverTime = 0;
  if (readEpoch(jsonFindMember(objectBegin, objectEnd, "time"), serverTime))
    feed.serverTime = static_cast<uint32_t>(serverTime);

  JsonArrayCursor cursor = jsonOpenArray(list);
  while (jsonNextItem(cursor) && feed.count < WEATHER_WARNING_MAX) {
    const char *itemBegin = cursor.itemBegin;
    const char *itemEnd = cursor.itemEnd;
    if (itemBegin >= itemEnd || *itemBegin != '{') continue;
    WeatherWarning warning;
    if (!readSmall(itemBegin, itemEnd, "lvl", warning.level)) continue;
    // Stupeň mimo žlutou až červenou je nesmysl; nemá barvu ani význam.
    if (warning.level < 2 || warning.level > 4) continue;
    readSmall(itemBegin, itemEnd, "type", warning.type);
    jsonCopyTextMember(itemBegin, itemEnd, "id", warning.id, sizeof(warning.id));
    jsonCopyTextMember(itemBegin, itemEnd, "ev", warning.event,
                       sizeof(warning.event));
    if (warning.event[0] == '\0') continue;
    // Podle id si hodiny pamatují, na co už upozornily. Bez něj poslouží jméno
    // jevu - stejné při každé aktualizaci téže výstrahy.
    if (warning.id[0] == '\0')
      strlcpy(warning.id, warning.event, sizeof(warning.id));
    if (!readEpoch(jsonFindMember(itemBegin, itemEnd, "on"), warning.onset))
      continue;
    readEpoch(jsonFindMember(itemBegin, itemEnd, "ex"), warning.expires);
    feed.items[feed.count++] = warning;
  }
  return true;
}

bool weatherWarningsBuildUrl(const char *baseUrl, float latitude,
                             float longitude, bool english, char *output,
                             size_t capacity) {
  if (baseUrl == nullptr || baseUrl[0] == '\0' || output == nullptr ||
      capacity == 0)
    return false;
  const char separator = strchr(baseUrl, '?') != nullptr ? '&' : '?';
  const int written =
      snprintf(output, capacity, "%s%clat=%.5f&lon=%.5f&lang=%s", baseUrl,
               separator, latitude, longitude, english ? "en" : "cs");
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool weatherWarningRelevant(const WeatherWarning &warning, int64_t now) {
  if (warning.expires != 0 && warning.expires <= now) return false;
  return warning.onset <= now + WEATHER_WARNING_SHOW_AHEAD_SECONDS;
}

const WeatherWarning *weatherWarningsTop(const WeatherWarningFeed &feed,
                                         uint8_t minimumLevel, int64_t now,
                                         uint8_t &others) {
  others = 0;
  const WeatherWarning *best = nullptr;
  for (size_t index = 0; index < feed.count; ++index) {
    const WeatherWarning &warning = feed.items[index];
    if (warning.level < minimumLevel || !weatherWarningRelevant(warning, now))
      continue;
    // Server řadí od nejvážnější, ale na to se nespoléhá: vyšší stupeň vyhrává,
    // při stejném ta, která platí už teď nebo začne dřív.
    if (best == nullptr || warning.level > best->level ||
        (warning.level == best->level && warning.onset < best->onset)) {
      if (best != nullptr) ++others;
      best = &warning;
    } else {
      ++others;
    }
  }
  return best;
}

void weatherWarningBannerTail(const WeatherWarning &warning, uint8_t others,
                              int64_t now, bool english, char *output,
                              size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  char when[32] = "";
  const char *word = "";
  if (warning.onset > now) {
    formatMoment(warning.onset, now, english, when, sizeof(when));
    word = english ? "from" : "od";
  } else if (warning.expires != 0) {
    formatMoment(warning.expires, now, english, when, sizeof(when));
    word = english ? "until" : "do";
  }
  char more[8] = "";
  if (others > 0)
    snprintf(more, sizeof(more), "  +%u", static_cast<unsigned>(others));
  if (when[0] != '\0')
    snprintf(output, capacity, " %s %s%s", word, when, more);
  else
    snprintf(output, capacity, "%s", more);
}

void weatherWarningBannerText(const WeatherWarning &warning, uint8_t others,
                              int64_t now, bool english, char *output,
                              size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  char tail[48];
  weatherWarningBannerTail(warning, others, now, english, tail, sizeof(tail));
  snprintf(output, capacity, "%s%s", warning.event, tail);
}

const WeatherWarning *weatherWarningsToAnnounce(
    const WeatherWarningFeed &feed, uint8_t switchLevel, int64_t now,
    const char alerted[][WEATHER_WARNING_ID_LENGTH], size_t alertedCount) {
  if (switchLevel == 0) return nullptr;
  const WeatherWarning *best = nullptr;
  for (size_t index = 0; index < feed.count; ++index) {
    const WeatherWarning &warning = feed.items[index];
    if (warning.level < switchLevel) continue;
    if (warning.expires != 0 && warning.expires <= now) continue;
    if (warning.onset > now + WEATHER_WARNING_SWITCH_AHEAD_SECONDS) continue;
    bool seen = false;
    for (size_t known = 0; known < alertedCount && !seen; ++known)
      seen = strcmp(alerted[known], warning.id) == 0;
    if (seen) continue;
    if (best == nullptr || warning.level > best->level) best = &warning;
  }
  return best;
}
