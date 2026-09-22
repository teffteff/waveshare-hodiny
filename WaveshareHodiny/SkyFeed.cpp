#include "SkyFeed.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "JsonScan.h"

namespace {

constexpr double DEGREES_TO_RADIANS = 0.017453292519943295;
constexpr double UNIX_EPOCH_JULIAN_DAY = 2440587.5;
constexpr double J2000_JULIAN_DAY = 2451545.0;

// Unixový čas z odpovědi. Do floatu by se nevešel bez ztráty sekund.
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

bool readFinite(const char *begin, const char *end, const char *key,
                float &out) {
  float number = 0.0f;
  if (!jsonReadNumberMember(begin, end, key, number) || !std::isfinite(number))
    return false;
  out = number;
  return true;
}

// Celé číslo z prvku plochého pole; mimo rozsah false.
bool readItemInteger(const JsonArrayCursor &cursor, long minimum, long maximum,
                     long &out) {
  JsonValue item;
  item.begin = cursor.itemBegin;
  item.end = cursor.itemEnd;
  float number = 0.0f;
  if (!jsonReadNumber(item, number) || !std::isfinite(number)) return false;
  const long rounded = lroundf(number);
  if (rounded < minimum || rounded > maximum) return false;
  out = rounded;
  return true;
}

JsonValue itemValue(const JsonArrayCursor &cursor) {
  JsonValue item;
  item.begin = cursor.itemBegin;
  item.end = cursor.itemEnd;
  item.isString = cursor.itemBegin < cursor.itemEnd && *cursor.itemBegin == '"';
  return item;
}

// Poloha na obloze z objektu {"ra":..,"dec":..}; mimo rozsah false.
bool readRaDec(const char *begin, const char *end, float &raHours,
               float &decDeg) {
  return readFinite(begin, end, "ra", raHours) &&
         readFinite(begin, end, "dec", decDeg) && raHours >= 0.0f &&
         raHours < 24.0f && decDeg >= -90.0f && decDeg <= 90.0f;
}

void parseMoonTrack(const JsonValue &value, SkyFeed &feed) {
  if (!value.isObject()) return;
  const char *begin = value.begin;
  const char *end = value.end;
  int64_t start = 0;
  float step = 0.0f;
  if (!readEpoch(jsonFindMember(begin, end, "t"), start) ||
      !jsonReadNumberMember(begin, end, "s", step) || step < 60.0f ||
      step > 86400.0f)
    return;
  JsonArrayCursor cursor = jsonOpenArray(jsonFindMember(begin, end, "p"));
  long pair[2] = {};
  int filled = 0;
  while (jsonNextItem(cursor) && feed.moonTrackCount < SKY_MOON_TRACK_POINTS) {
    static const long MINIMUM[2] = {0, -9000};
    static const long MAXIMUM[2] = {23999, 9000};
    if (!readItemInteger(cursor, MINIMUM[filled], MAXIMUM[filled],
                         pair[filled]))
      break;
    if (++filled < 2) continue;
    filled = 0;
    SkyTrackPoint &point = feed.moonTrack[feed.moonTrackCount++];
    point.raMilliHours = static_cast<int16_t>(pair[0]);
    point.decCentiDeg = static_cast<int16_t>(pair[1]);
  }
  feed.moonTrackStart = start;
  feed.moonTrackStep = static_cast<uint32_t>(lroundf(step));
}

// Úsek [od, do] v unixových sekundách; vadný nechá obě hodnoty nulové.
void parseWindow(const JsonValue &value, int64_t &from, int64_t &to) {
  JsonArrayCursor cursor = jsonOpenArray(value);
  int64_t edges[2] = {};
  bool valid = true;
  for (int64_t &edge : edges)
    valid = valid && jsonNextItem(cursor) && readEpoch(itemValue(cursor), edge);
  if (!valid || edges[0] >= edges[1]) return;
  from = edges[0];
  to = edges[1];
}

void parseBody(const char *begin, const char *end, SkyFeed &feed) {
  SkyBody body;
  if (!readFinite(begin, end, "ra", body.raHours) ||
      !readFinite(begin, end, "dec", body.decDeg))
    return;
  if (body.raHours < 0.0f || body.raHours >= 24.0f || body.decDeg < -90.0f ||
      body.decDeg > 90.0f)
    return;
  jsonCopyTextMember(begin, end, "id", body.id, sizeof(body.id));
  jsonCopyTextMember(begin, end, "n", body.name, sizeof(body.name));
  if (body.name[0] == '\0') return;
  float kind = SKY_BODY_PLANET;
  if (jsonReadNumberMember(begin, end, "k", kind) && kind >= 0.0f &&
      kind <= 2.0f)
    body.kind = static_cast<uint8_t>(lroundf(kind));
  body.hasMagnitude = readFinite(begin, end, "mag", body.magnitude);
  readFinite(begin, end, "ill", body.illumination);
  if (body.illumination < 0.0f) body.illumination = 0.0f;
  if (body.illumination > 1.0f) body.illumination = 1.0f;
  float distance = 0.0f;
  if (readFinite(begin, end, "dist", distance) && distance > 0.0f)
    body.distanceKm = static_cast<uint32_t>(distance);
  readFinite(begin, end, "au", body.distanceAu);
  jsonCopyTextMember(begin, end, "con", body.constellation,
                     sizeof(body.constellation));
  readEpoch(jsonFindMember(begin, end, "rise"), body.rise);
  readEpoch(jsonFindMember(begin, end, "set"), body.set);
  feed.bodies[feed.bodyCount++] = body;
}

}  // namespace

bool skyFeedParse(const char *begin, const char *end, SkyFeed &feed) {
  feed = SkyFeed{};
  if (begin == nullptr || end == nullptr || begin >= end) return false;
  const char *objectBegin = jsonSkipWhitespace(begin, end);
  if (objectBegin >= end || *objectBegin != '{') return false;
  const char *objectEnd = jsonValueEnd(objectBegin, end);
  if (objectEnd == nullptr) return false;

  float version = 0.0f;
  if (!jsonReadNumberMember(objectBegin, objectEnd, "v", version) ||
      lroundf(version) != 1)
    return false;
  const JsonValue bodies = jsonFindMember(objectBegin, objectEnd, "bodies");
  if (!bodies.isArray()) return false;

  int64_t serverTime = 0;
  if (readEpoch(jsonFindMember(objectBegin, objectEnd, "time"), serverTime))
    feed.serverTime = static_cast<uint32_t>(serverTime);
  feed.hasKp = readFinite(objectBegin, objectEnd, "kp", feed.kp) &&
               feed.kp >= 0.0f && feed.kp <= 9.0f;
  feed.hasKpMax = readFinite(objectBegin, objectEnd, "kpMax", feed.kpMax) &&
                  feed.kpMax >= 0.0f && feed.kpMax <= 9.0f;
  readEpoch(jsonFindMember(objectBegin, objectEnd, "kpMaxAt"), feed.kpMaxAt);

  JsonArrayCursor cursor = jsonOpenArray(bodies);
  while (jsonNextItem(cursor) && feed.bodyCount < SKY_MAX_BODIES) {
    if (cursor.itemBegin < cursor.itemEnd && *cursor.itemBegin == '{')
      parseBody(cursor.itemBegin, cursor.itemEnd, feed);
  }

  // Hvězdy po trojicích. Rozbitá trojice ukončí seznam: další čísla by se
  // posunula a hvězdy by ležely jinde.
  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "stars"));
  long values[3] = {};
  int filled = 0;
  while (jsonNextItem(cursor) && feed.starCount < SKY_MAX_STARS) {
    static const long MINIMUM[3] = {0, -9000, -30};
    static const long MAXIMUM[3] = {23999, 9000, 99};
    if (!readItemInteger(cursor, MINIMUM[filled], MAXIMUM[filled],
                         values[filled]))
      break;
    if (++filled < 3) continue;
    filled = 0;
    SkyStar &star = feed.stars[feed.starCount++];
    star.raMilliHours = static_cast<int16_t>(values[0]);
    star.decCentiDeg = static_cast<int16_t>(values[1]);
    star.magnitudeTenths = static_cast<int8_t>(values[2]);
  }

  // Čáry po dvojicích; index mimo hvězdy se zahodí.
  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "lines"));
  long first = 0;
  bool haveFirst = false;
  while (jsonNextItem(cursor) && feed.lineCount < SKY_MAX_LINES) {
    long index = 0;
    if (!readItemInteger(cursor, 0, 255, index)) break;
    if (!haveFirst) {
      first = index;
      haveFirst = true;
      continue;
    }
    haveFirst = false;
    if (static_cast<size_t>(first) >= feed.starCount ||
        static_cast<size_t>(index) >= feed.starCount)
      continue;
    feed.lines[feed.lineCount].first = static_cast<uint8_t>(first);
    feed.lines[feed.lineCount].second = static_cast<uint8_t>(index);
    ++feed.lineCount;
  }

  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "events"));
  while (jsonNextItem(cursor) && feed.eventCount < SKY_MAX_EVENTS) {
    const char *itemBegin = cursor.itemBegin;
    const char *itemEnd = cursor.itemEnd;
    if (itemBegin >= itemEnd || *itemBegin != '{') continue;
    SkyEvent event;
    if (!readEpoch(jsonFindMember(itemBegin, itemEnd, "t"), event.time)) continue;
    float hasTime = 0.0f;
    event.hasTime = jsonReadNumberMember(itemBegin, itemEnd, "tm", hasTime) &&
                    hasTime > 0.5f;
    jsonCopyTextMember(itemBegin, itemEnd, "x", event.text, sizeof(event.text));
    if (event.text[0] == '\0') continue;
    feed.events[feed.eventCount++] = event;
  }

  // Jména hvězd a souhvězdí ve stejném pořadí jako hvězdy.
  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "starNames"));
  for (size_t index = 0; index < feed.starCount && jsonNextItem(cursor); ++index)
    jsonCopyText(itemValue(cursor), feed.stars[index].name,
                 sizeof(feed.stars[index].name));
  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "starCons"));
  for (size_t index = 0; index < feed.starCount && jsonNextItem(cursor); ++index)
    jsonCopyText(itemValue(cursor), feed.stars[index].constellation,
                 sizeof(feed.stars[index].constellation));

  cursor = jsonOpenArray(jsonFindMember(objectBegin, objectEnd, "cons"));
  while (jsonNextItem(cursor) && feed.figureCount < SKY_MAX_FIGURES) {
    if (cursor.itemBegin >= cursor.itemEnd || *cursor.itemBegin != '{') continue;
    SkyFigure figure;
    if (!readRaDec(cursor.itemBegin, cursor.itemEnd, figure.raHours,
                   figure.decDeg))
      continue;
    jsonCopyTextMember(cursor.itemBegin, cursor.itemEnd, "n", figure.name,
                       sizeof(figure.name));
    if (figure.name[0] == '\0') continue;
    feed.figures[feed.figureCount++] = figure;
  }

  parseMoonTrack(jsonFindMember(objectBegin, objectEnd, "moonTrack"), feed);

  parseWindow(jsonFindMember(objectBegin, objectEnd, "dark"), feed.darkFrom,
              feed.darkTo);
  parseWindow(jsonFindMember(objectBegin, objectEnd, "day"), feed.dayFrom,
              feed.dayTo);

  const JsonValue radiant = jsonFindMember(objectBegin, objectEnd, "radiant");
  if (radiant.isObject() &&
      readRaDec(radiant.begin, radiant.end, feed.radiantRaHours,
                feed.radiantDecDeg)) {
    jsonCopyTextMember(radiant.begin, radiant.end, "n", feed.radiantName,
                       sizeof(feed.radiantName));
    feed.hasRadiant = feed.radiantName[0] != '\0';
  }
  return true;
}

bool skyFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                     bool english, char *output, size_t capacity) {
  if (baseUrl == nullptr || baseUrl[0] == '\0' || output == nullptr ||
      capacity == 0)
    return false;
  const char separator = strchr(baseUrl, '?') != nullptr ? '&' : '?';
  const int written =
      snprintf(output, capacity, "%s%cview=sky&lat=%.4f&lon=%.4f&lang=%s",
               baseUrl, separator, latitude, longitude, english ? "en" : "cs");
  return written > 0 && static_cast<size_t>(written) < capacity;
}

void skyHorizontal(double raHours, double decDeg, double latitudeDeg,
                   double longitudeDeg, double epoch, float &azimuthDeg,
                   float &altitudeDeg) {
  // Střední hvězdný čas v Greenwichi (Meeus 12.4 bez kvadratického členu,
  // který za sto let udělá desetinu sekundy).
  const double julianDay = epoch / 86400.0 + UNIX_EPOCH_JULIAN_DAY;
  double sidereal =
      280.46061837 + 360.98564736629 * (julianDay - J2000_JULIAN_DAY);
  sidereal = std::fmod(sidereal + longitudeDeg, 360.0);
  if (sidereal < 0.0) sidereal += 360.0;
  const double hourAngle = (sidereal - raHours * 15.0) * DEGREES_TO_RADIANS;
  const double latitude = latitudeDeg * DEGREES_TO_RADIANS;
  const double declination = decDeg * DEGREES_TO_RADIANS;
  const double sinAltitude = std::sin(latitude) * std::sin(declination) +
                             std::cos(latitude) * std::cos(declination) *
                                 std::cos(hourAngle);
  double altitude =
      std::asin(sinAltitude < -1.0 ? -1.0 : sinAltitude > 1.0 ? 1.0 : sinAltitude) /
      DEGREES_TO_RADIANS;
  double azimuth =
      std::atan2(-std::cos(declination) * std::sin(hourAngle),
                 std::sin(declination) * std::cos(latitude) -
                     std::cos(declination) * std::cos(hourAngle) *
                         std::sin(latitude)) /
      DEGREES_TO_RADIANS;
  if (azimuth < 0.0) azimuth += 360.0;
  // Refrakce podle Bennetta, v úhlových minutách. Pod stupeň pod obzorem už
  // nic neviditelného neukazuje, tak se neřeší.
  if (altitude > -1.0) {
    const double refraction =
        1.0 / std::tan((altitude + 7.31 / (altitude + 4.4)) * DEGREES_TO_RADIANS);
    altitude += refraction / 60.0;
  }
  azimuthDeg = static_cast<float>(azimuth);
  altitudeDeg = static_cast<float>(altitude);
}

void skyEclipticPoint(float longitudeDeg, float &raHours, float &decDeg) {
  constexpr double OBLIQUITY = 23.4393 * DEGREES_TO_RADIANS;
  const double longitude = longitudeDeg * DEGREES_TO_RADIANS;
  double ra = std::atan2(std::sin(longitude) * std::cos(OBLIQUITY),
                         std::cos(longitude)) /
              DEGREES_TO_RADIANS / 15.0;
  if (ra < 0.0) ra += 24.0;
  raHours = static_cast<float>(ra);
  decDeg = static_cast<float>(
      std::asin(std::sin(OBLIQUITY) * std::sin(longitude)) / DEGREES_TO_RADIANS);
}

void skyEventWhen(const SkyEvent &event, int64_t now, bool english,
                  char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  const time_t eventTime = static_cast<time_t>(event.time);
  const time_t nowTime = static_cast<time_t>(now);
  struct tm local;
  struct tm today;
  localtime_r(&eventTime, &local);
  localtime_r(&nowTime, &today);
  struct tm tomorrow = today;
  tomorrow.tm_mday += 1;
  tomorrow.tm_hour = 12;
  const time_t tomorrowTime = mktime(&tomorrow);
  localtime_r(&tomorrowTime, &tomorrow);
  const bool sameDay =
      local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;
  const bool nextDay =
      local.tm_year == tomorrow.tm_year && local.tm_yday == tomorrow.tm_yday;
  char clock[8] = "";
  // Hodina se píše jen dnes a zítra: u vzdálenějšího úkazu by řádek přetekl
  // a na minutě stejně ještě nezáleží.
  if (event.hasTime && (sameDay || nextDay))
    snprintf(clock, sizeof(clock), " %02d:%02d", local.tm_hour, local.tm_min);
  if (sameDay) {
    snprintf(output, capacity, "%s%s", english ? "TODAY" : "DNES", clock);
  } else if (nextDay) {
    snprintf(output, capacity, "%s%s", english ? "TMRW" : "ZÍTRA", clock);
  } else if (english) {
    static const char *const MONTHS[12] = {"JAN", "FEB", "MAR", "APR",
                                           "MAY", "JUN", "JUL", "AUG",
                                           "SEP", "OCT", "NOV", "DEC"};
    if (local.tm_year != today.tm_year)
      snprintf(output, capacity, "%s %d %d", MONTHS[local.tm_mon % 12],
               local.tm_mday, local.tm_year + 1900);
    else
      snprintf(output, capacity, "%s %d", MONTHS[local.tm_mon % 12],
               local.tm_mday);
  } else if (local.tm_year != today.tm_year) {
    snprintf(output, capacity, "%d. %d. %d", local.tm_mday, local.tm_mon + 1,
             local.tm_year + 1900);
  } else {
    snprintf(output, capacity, "%d. %d.", local.tm_mday, local.tm_mon + 1);
  }
}
