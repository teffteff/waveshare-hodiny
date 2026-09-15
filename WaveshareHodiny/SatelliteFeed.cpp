#include "SatelliteFeed.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "JsonScan.h"

namespace {
constexpr float DEG = 3.14159265358979f / 180.0f;
// Čas před listopadem 2023 znamená rozbitou odpověď, ne starou.
constexpr int64_t MIN_EPOCH = 1700000000LL;
constexpr int64_t MAX_EPOCH = 4102444800LL;  // rok 2100
constexpr uint16_t MAX_STEP_SECONDS = 120;

constexpr const char *GROUP_NAMES[SATELLITE_GROUP_COUNT] = {
    "stations", "visual", "weather", "gnss", "amateur", "starlink"};

// Celé číslo i se znaménkem. JsonScan čte do floatu, který by unixový čas
// zaokrouhlil o dvě minuty.
bool readInteger(const JsonValue &value, int64_t &out) {
  if (!value.valid() || value.isString) return false;
  const char *cursor = value.begin;
  const char *end = value.end;
  bool negative = false;
  if (cursor < end && *cursor == '-') {
    negative = true;
    ++cursor;
  }
  int64_t result = 0;
  bool digits = false;
  for (; cursor < end && *cursor >= '0' && *cursor <= '9'; ++cursor) {
    if (result > (INT64_MAX - 9) / 10) return false;
    result = result * 10 + (*cursor - '0');
    digits = true;
  }
  if (!digits || cursor != end) return false;
  out = negative ? -result : result;
  return true;
}

bool readIntegerMember(const char *objectBegin, const char *objectEnd,
                       const char *key, int64_t &out) {
  return readInteger(jsonFindMember(objectBegin, objectEnd, key), out);
}

uint16_t clampU16(int64_t value) {
  if (value < 0) return 0;
  return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
}

// Jméno do bufferu jen z tisknutelného ASCII. Server jiné znaky neposílá, ale
// popisek se kreslí písmem bez diakritiky a nic jiného by na displeji nebylo.
void copyName(const char *objectBegin, const char *objectEnd, char *output,
              size_t capacity) {
  jsonCopyTextMember(objectBegin, objectEnd, "n", output, capacity);
  for (char *cursor = output; *cursor != '\0'; ++cursor) {
    if (*cursor < 0x20 || *cursor > 0x7e) *cursor = '?';
  }
}

bool parseTrack(const char *itemBegin, const char *itemEnd, uint8_t sampleCount,
                SatelliteTrack &track) {
  int64_t id = 0;
  int64_t group = 0;
  if (!readIntegerMember(itemBegin, itemEnd, "id", id) || id <= 0 ||
      id > UINT32_MAX)
    return false;
  if (!readIntegerMember(itemBegin, itemEnd, "g", group) || group < 0 ||
      group >= SATELLITE_GROUP_COUNT)
    return false;
  const JsonValue samples = jsonFindMember(itemBegin, itemEnd, "p");
  JsonArrayCursor cursor = jsonOpenArray(samples);
  if (!cursor.valid()) return false;
  size_t read = 0;
  while (jsonNextItem(cursor)) {
    if (read >= 2U * sampleCount) return false;
    JsonValue item;
    item.begin = cursor.itemBegin;
    item.end = cursor.itemEnd;
    int64_t value = 0;
    if (!readInteger(item, value)) return false;
    const size_t index = read / 2;
    if (read % 2 == 0) {
      if (value < 0 || value >= 3600) return false;
      track.azimuthTenths[index] = static_cast<int16_t>(value);
    } else {
      if (value < -900 || value > 900) return false;
      track.elevationTenths[index] = static_cast<int16_t>(value);
    }
    ++read;
  }
  if (read != 2U * sampleCount) return false;

  track.noradId = static_cast<uint32_t>(id);
  track.group = static_cast<uint8_t>(group);
  copyName(itemBegin, itemEnd, track.name, sizeof(track.name));
  int64_t number = 0;
  track.altitudeKm =
      readIntegerMember(itemBegin, itemEnd, "h", number) ? clampU16(number) : 0;
  track.rangeKm =
      readIntegerMember(itemBegin, itemEnd, "r", number) ? clampU16(number) : 0;
  track.sunlit =
      readIntegerMember(itemBegin, itemEnd, "l", number) && number == 1;
  return true;
}

void parsePass(const JsonValue &value, SatellitePass &pass) {
  pass = SatellitePass{};
  if (!value.isObject()) return;
  const char *begin = value.begin;
  const char *end = value.end;
  int64_t rise = 0;
  int64_t set = 0;
  int64_t maxTime = 0;
  int64_t maxElevation = 0;
  if (!readIntegerMember(begin, end, "rise", rise) ||
      !readIntegerMember(begin, end, "set", set) ||
      !readIntegerMember(begin, end, "max", maxElevation))
    return;
  if (rise < MIN_EPOCH || rise > MAX_EPOCH || set <= rise ||
      set - rise > 3600 || maxElevation < 0 || maxElevation > 90)
    return;
  if (!readIntegerMember(begin, end, "maxTime", maxTime) || maxTime < rise ||
      maxTime > set)
    maxTime = rise + (set - rise) / 2;
  int64_t visible = 0;
  pass.rise = rise;
  pass.set = set;
  pass.maxTime = maxTime;
  pass.maxElevationDeg = static_cast<uint8_t>(maxElevation);
  pass.visible = readIntegerMember(begin, end, "vis", visible) && visible == 1;
  pass.valid = true;
}

}  // namespace

const char *satelliteGroupName(uint8_t group) {
  return group < SATELLITE_GROUP_COUNT ? GROUP_NAMES[group] : nullptr;
}

SatelliteParseStatus satelliteParseFeed(const char *begin, const char *end,
                                        SatelliteTrack *tracks, size_t capacity,
                                        SatelliteFeedInfo &info) {
  info = SatelliteFeedInfo{};
  const char *objectBegin = jsonSkipWhitespace(begin, end);
  if (objectBegin == nullptr || objectBegin >= end || *objectBegin != '{')
    return SatelliteParseStatus::NotJson;
  const char *objectEnd = jsonValueEnd(objectBegin, end);
  if (objectEnd == nullptr) return SatelliteParseStatus::NotJson;

  int64_t version = 0;
  int64_t start = 0;
  int64_t step = 0;
  int64_t samples = 0;
  // Jiná verze by mohla nést tatáž jména klíčů s jiným významem.
  if (!readIntegerMember(objectBegin, objectEnd, "v", version) || version != 1)
    return SatelliteParseStatus::Invalid;
  if (!readIntegerMember(objectBegin, objectEnd, "time", start) ||
      start < MIN_EPOCH || start > MAX_EPOCH ||
      !readIntegerMember(objectBegin, objectEnd, "step", step) || step < 1 ||
      step > MAX_STEP_SECONDS ||
      !readIntegerMember(objectBegin, objectEnd, "samples", samples) ||
      samples < 2 || samples > static_cast<int64_t>(SATELLITE_MAX_SAMPLES))
    return SatelliteParseStatus::Invalid;
  const JsonValue list = jsonFindMember(objectBegin, objectEnd, "sats");
  if (!list.isArray()) return SatelliteParseStatus::Invalid;

  info.startEpoch = start;
  info.stepSeconds = static_cast<uint16_t>(step);
  info.sampleCount = static_cast<uint8_t>(samples);
  int64_t number = 0;
  if (readIntegerMember(objectBegin, objectEnd, "sun", number) &&
      number >= -900 && number <= 900) {
    info.hasSunElevation = true;
    info.sunElevationDeg = static_cast<float>(number) / 10.0f;
  }
  if (readIntegerMember(objectBegin, objectEnd, "total", number))
    info.total = clampU16(number);
  if (readIntegerMember(objectBegin, objectEnd, "age", number))
    info.ageHours = clampU16(number);
  jsonCopyTextMember(objectBegin, objectEnd, "problem", info.problem,
                     sizeof(info.problem));
  const JsonValue pending = jsonFindMember(objectBegin, objectEnd, "pending");
  if (pending.isArray()) {
    JsonArrayCursor cursor = jsonOpenArray(pending);
    info.pending = jsonNextItem(cursor);
  }
  parsePass(jsonFindMember(objectBegin, objectEnd, "pass"), info.pass);

  JsonArrayCursor cursor = jsonOpenArray(list);
  while (info.count < capacity && jsonNextItem(cursor)) {
    SatelliteTrack track;
    if (!parseTrack(cursor.itemBegin, cursor.itemEnd, info.sampleCount, track))
      continue;
    if (tracks != nullptr) tracks[info.count] = track;
    ++info.count;
  }
  return SatelliteParseStatus::Ok;
}

bool satelliteFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                           uint8_t groupMask, uint8_t minElevationDeg,
                           char *output, size_t capacity) {
  if (baseUrl == nullptr || baseUrl[0] == '\0' || output == nullptr ||
      capacity == 0)
    return false;
  if (!std::isfinite(latitude) || !std::isfinite(longitude)) return false;
  char groups[64] = "";
  size_t length = 0;
  for (uint8_t group = 0; group < SATELLITE_GROUP_COUNT; ++group) {
    if ((groupMask & (1U << group)) == 0) continue;
    const int written = snprintf(groups + length, sizeof(groups) - length,
                                 "%s%s", length > 0 ? "," : "",
                                 GROUP_NAMES[group]);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(groups) - length)
      return false;
    length += static_cast<size_t>(written);
  }
  if (length == 0) return false;
  const char separator = strchr(baseUrl, '?') != nullptr ? '&' : '?';
  // Dvě desetinná místa jsou kilometr. Na obloze je to pod stupeň a server
  // polohu stejně zaokrouhlí; přesnější poloha by jen zbytečně ležela v logu.
  const int written = snprintf(
      output, capacity, "%s%clat=%.2f&lon=%.2f&groups=%s&minel=%u", baseUrl,
      separator, static_cast<double>(latitude), static_cast<double>(longitude),
      groups, static_cast<unsigned>(minElevationDeg));
  return written > 0 && static_cast<size_t>(written) < capacity;
}

void satelliteSkyProject(float azimuthDeg, float elevationDeg, float &x,
                         float &y) {
  const float radius = (90.0f - elevationDeg) / 90.0f;
  x = radius * std::sin(azimuthDeg * DEG);
  y = -radius * std::cos(azimuthDeg * DEG);
}

double satelliteFeedEndEpoch(const SatelliteFeedInfo &info) {
  if (info.sampleCount < 2) return static_cast<double>(info.startEpoch);
  return static_cast<double>(info.startEpoch) +
         static_cast<double>(info.stepSeconds) * (info.sampleCount - 1);
}

bool satelliteTrackAt(const SatelliteTrack &track, const SatelliteFeedInfo &info,
                      double epoch, SatelliteSkyPoint &point) {
  if (info.sampleCount < 2 || info.stepSeconds == 0 || !std::isfinite(epoch))
    return false;
  const double offset = epoch - static_cast<double>(info.startEpoch);
  const double span =
      static_cast<double>(info.stepSeconds) * (info.sampleCount - 1);
  if (offset < 0.0 || offset > span) return false;
  size_t index = static_cast<size_t>(offset / info.stepSeconds);
  if (index >= static_cast<size_t>(info.sampleCount - 1))
    index = info.sampleCount - 2;
  const float fraction = static_cast<float>(
      (offset - static_cast<double>(index) * info.stepSeconds) /
      info.stepSeconds);

  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;
  satelliteSkyProject(track.azimuthTenths[index] / 10.0f,
                      track.elevationTenths[index] / 10.0f, x0, y0);
  satelliteSkyProject(track.azimuthTenths[index + 1] / 10.0f,
                      track.elevationTenths[index + 1] / 10.0f, x1, y1);
  point.x = x0 + (x1 - x0) * fraction;
  point.y = y0 + (y1 - y0) * fraction;
  point.elevationDeg = (track.elevationTenths[index] +
                        (track.elevationTenths[index + 1] -
                         track.elevationTenths[index]) *
                            fraction) /
                       10.0f;
  float azimuth = std::atan2(point.x, -point.y) / DEG;
  if (azimuth < 0.0f) azimuth += 360.0f;
  // Přímo v zenitu azimut nic neznamená; vezme se ten ze serveru.
  if (point.x * point.x + point.y * point.y < 1e-6f)
    azimuth = track.azimuthTenths[index] / 10.0f;
  point.azimuthDeg = azimuth;
  return true;
}

void satelliteShortName(const char *name, char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  output[0] = '\0';
  if (name == nullptr) return;
  size_t length = strlen(name);
  const char *bracket = strstr(name, " (");
  // Závorka hned na začátku by nechala prázdný popisek; pak zůstane celé jméno.
  if (bracket != nullptr && bracket != name) length = bracket - name;
  while (length > 0 && name[length - 1] == ' ') --length;
  if (length >= capacity) length = capacity - 1;
  memcpy(output, name, length);
  output[length] = '\0';
}
