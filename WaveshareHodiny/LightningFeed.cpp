#include "LightningFeed.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "JsonScan.h"

namespace {
constexpr double EARTH_RADIUS_KM = 6371.0088;
constexpr double DEG = 3.14159265358979323846 / 180.0;
// Čas úderu před listopadem 2023 nebo neplatné číslo znamená rozbitou zprávu.
constexpr int64_t MIN_STROKE_EPOCH_MS = 1700000000000LL;

// Celé nezáporné číslo z JSON hodnoty. JsonScan čte do floatu, který by
// milisekundový čas zaokrouhlil o minuty.
bool readInteger(const JsonValue &value, int64_t &out) {
  if (!value.valid()) return false;
  const char *cursor = value.contentBegin();
  const char *end = value.contentEnd();
  if (cursor >= end) return false;
  int64_t result = 0;
  bool digits = false;
  for (; cursor < end && *cursor >= '0' && *cursor <= '9'; ++cursor) {
    if (result > (INT64_MAX - 9) / 10) return false;
    result = result * 10 + (*cursor - '0');
    digits = true;
  }
  // Desetinnou část čas úderu nemá, ale kdyby ji server přidal, zahodí se.
  if (cursor < end && *cursor == '.') {
    ++cursor;
    while (cursor < end && *cursor >= '0' && *cursor <= '9') ++cursor;
  }
  if (!digits || cursor != end) return false;
  out = result;
  return true;
}

bool readDouble(const JsonValue &value, double &out) {
  if (!value.valid() || value.isString) return false;
  // Hodnota v bufferu nekončí nulou; číslo v JSONu je krátké, takže se
  // zkopíruje.
  char text[40];
  const size_t length = static_cast<size_t>(value.end - value.begin);
  if (length == 0 || length >= sizeof(text)) return false;
  for (size_t index = 0; index < length; ++index) text[index] = value.begin[index];
  text[length] = '\0';
  char *parsedEnd = nullptr;
  const double result = strtod(text, &parsedEnd);
  if (parsedEnd != text + length || !std::isfinite(result)) return false;
  out = result;
  return true;
}

}  // namespace

LightningMessageKind lightningParseMessage(const char *begin, const char *end,
                                           LightningMessageInfo &info,
                                           LightningStrokeSink sink,
                                           void *context) {
  info = LightningMessageInfo{};
  const char *objectBegin = jsonSkipWhitespace(begin, end);
  if (objectBegin == nullptr || objectBegin >= end || *objectBegin != '{')
    return info.kind;
  const char *objectEnd = jsonValueEnd(objectBegin, end);
  if (objectEnd == nullptr) return info.kind;

  const JsonValue challenge = jsonFindMember(objectBegin, objectEnd, "k");
  if (readDouble(challenge, info.challengeKey)) info.hasChallenge = true;

  const JsonValue live = jsonFindMember(objectBegin, objectEnd, "live");
  if (live.valid() && !live.isString)
    info.live = jsonReadBoolMember(objectBegin, objectEnd, "live");

  const bool haveTime = readDouble(
      jsonFindMember(objectBegin, objectEnd, "time"), info.serverTime);
  const JsonValue strokes = jsonFindMember(objectBegin, objectEnd, "strokes");
  if (!strokes.isArray()) {
    if (!haveTime) return info.kind;
    info.kind = info.hasChallenge ? LightningMessageKind::Hello
                                  : LightningMessageKind::Heartbeat;
    return info.kind;
  }

  JsonArrayCursor cursor = jsonOpenArray(strokes);
  while (jsonNextItem(cursor)) {
    if (info.strokeCount < UINT16_MAX) ++info.strokeCount;
    float latitude = NAN;
    float longitude = NAN;
    int64_t timeMs = 0;
    if (!jsonReadNumberMember(cursor.itemBegin, cursor.itemEnd, "lat",
                              latitude) ||
        !jsonReadNumberMember(cursor.itemBegin, cursor.itemEnd, "lon",
                              longitude) ||
        !readInteger(jsonFindMember(cursor.itemBegin, cursor.itemEnd, "time"),
                     timeMs))
      continue;
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        std::fabs(latitude) > 90.0f || std::fabs(longitude) > 180.0f ||
        timeMs < MIN_STROKE_EPOCH_MS)
      continue;
    LightningStroke stroke;
    stroke.latitude = latitude;
    stroke.longitude = longitude;
    stroke.epochSeconds = static_cast<uint32_t>(timeMs / 1000);
    int64_t id = 0;
    if (readInteger(jsonFindMember(cursor.itemBegin, cursor.itemEnd, "id"), id))
      stroke.id = static_cast<uint32_t>(id);
    if (info.validStrokeCount < UINT16_MAX) ++info.validStrokeCount;
    if (sink != nullptr) sink(stroke, context);
  }
  // Rozbité pole (useknutá zpráva) projde jen do místa, kde se rozbilo; co se
  // přečetlo, platí.
  info.kind = LightningMessageKind::Strokes;
  return info.kind;
}

bool lightningFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                           float radiusKm, double serverCursor,
                           char *output, size_t capacity) {
  if (baseUrl == nullptr || baseUrl[0] == '\0') return false;
  const char separator = strchr(baseUrl, '?') != nullptr ? '&' : '?';
  const int written =
      snprintf(output, capacity, "%s%clat=%.4f&lon=%.4f&r=%.0f&since=%.3f",
               baseUrl, separator, latitude, longitude, std::ceil(radiusKm),
               serverCursor > 0.0 ? serverCursor : 0.0);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

float lightningDistanceKm(float latitudeA, float longitudeA, float latitudeB,
                          float longitudeB) {
  const double latA = latitudeA * DEG;
  const double latB = latitudeB * DEG;
  const double halfLat = (latitudeB - latitudeA) * DEG * 0.5;
  const double halfLon = (longitudeB - longitudeA) * DEG * 0.5;
  double a = std::sin(halfLat) * std::sin(halfLat) +
             std::cos(latA) * std::cos(latB) * std::sin(halfLon) *
                 std::sin(halfLon);
  if (a > 1.0) a = 1.0;
  return static_cast<float>(2.0 * EARTH_RADIUS_KM *
                            std::atan2(std::sqrt(a), std::sqrt(1.0 - a)));
}
