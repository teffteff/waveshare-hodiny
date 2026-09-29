#include "PushAlerts.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "JsonScan.h"

namespace {
// Celé číslo z oddílu v mezích; mimo ně je odpověď rozbitá.
bool readInteger(const JsonValue &section, const char *key, long low, long high,
                 long &out) {
  float value = 0.0f;
  if (!jsonReadNumberMember(section.begin, section.end, key, value))
    return false;
  if (value < static_cast<float>(low) || value > static_cast<float>(high))
    return false;
  out = static_cast<long>(value);
  return static_cast<float>(out) == value;
}

bool readSwitch(const JsonValue &section, bool &out) {
  const JsonValue value = jsonFindMember(section.begin, section.end, "enabled");
  if (!value.valid() || value.isString) return false;
  const size_t length = static_cast<size_t>(value.end - value.begin);
  if (length == 4 && strncmp(value.begin, "true", 4) == 0) {
    out = true;
    return true;
  }
  if (length == 5 && strncmp(value.begin, "false", 5) == 0) {
    out = false;
    return true;
  }
  return false;
}

JsonValue section(const JsonValue &settings, const char *key) {
  const JsonValue value = jsonFindMember(settings.begin, settings.end, key);
  return value.isObject() ? value : JsonValue();
}
}  // namespace

bool pushAlertsIdFromMac(const char *mac, char *id, size_t capacity) {
  if (mac == nullptr || id == nullptr || capacity < PUSH_ALERTS_ID_LENGTH)
    return false;
  size_t length = 0;
  for (const char *cursor = mac; *cursor != '\0'; ++cursor) {
    if (*cursor == ':') continue;
    if (!isxdigit(static_cast<unsigned char>(*cursor)) || length >= 12)
      return false;
    id[length++] = static_cast<char>(tolower(static_cast<unsigned char>(*cursor)));
  }
  id[length] = '\0';
  return length == 12;
}

bool pushAlertsBuildUrl(const char *base, const char *id, float latitude,
                        float longitude, char *url, size_t capacity) {
  if (base == nullptr || id == nullptr || base[0] == '\0' || id[0] == '\0')
    return false;
  if (!(latitude >= -90.0f && latitude <= 90.0f) ||
      !(longitude >= -180.0f && longitude <= 180.0f))
    return false;
  size_t baseLength = strlen(base);
  while (baseLength > 0 && base[baseLength - 1] == '/') --baseLength;
  const int written =
      snprintf(url, capacity, "%.*s/config/%s?lat=%.5f&lon=%.5f",
               static_cast<int>(baseLength), base, id,
               static_cast<double>(latitude), static_cast<double>(longitude));
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool pushAlertsParseAnswer(const char *begin, const char *end,
                           PushAlertsServerSettings &out) {
  out = PushAlertsServerSettings();
  if (begin == nullptr || end == nullptr) return false;
  const char *start = jsonSkipWhitespace(begin, end);
  if (start == nullptr || start >= end || *start != '{') return false;
  const char *finish = jsonValueEnd(start, end);
  if (finish == nullptr) return false;

  float elevation = 0.0f;
  if (jsonReadNumberMember(start, finish, "elevation", elevation) &&
      elevation > -500.0f && elevation < 9000.0f) {
    out.elevationM = elevation;
    out.elevationKnown = true;
  }
  char portal[PUSH_ALERTS_PORTAL_LENGTH];
  jsonCopyTextMember(start, finish, "portal", portal, sizeof(portal));
  // Odkaz se ukazuje na stránce nastavení, takže jen obyčejná https adresa.
  bool plain = strncmp(portal, "https://", 8) == 0;
  for (const char *cursor = portal; plain && *cursor != '\0'; ++cursor) {
    const unsigned char value = static_cast<unsigned char>(*cursor);
    plain = value > ' ' && value < 0x7f && value != '"' && value != '<' &&
            value != '>' && value != '\\' && value != '@';
  }
  if (plain) strlcpy(out.portal, portal, sizeof(out.portal));

  const JsonValue settings = jsonFindMember(start, finish, "settings");
  if (!settings.valid()) return false;
  if (!settings.isObject()) {
    // null: server ještě nezná polohu domu.
    return settings.end - settings.begin == 4 &&
           strncmp(settings.begin, "null", 4) == 0;
  }

  const JsonValue rain = section(settings, "rain");
  const JsonValue military = section(settings, "military");
  const JsonValue rare = section(settings, "rare");
  const JsonValue low = section(settings, "low");
  const JsonValue circling = section(settings, "circling");
  const JsonValue warnings = section(settings, "warnings");
  const JsonValue quiet = section(settings, "quiet");
  long lead, dbz, rainRadius, militaryRadius, rareRadius, distance, height,
      circlingRadius, level, from, to;
  PushAlertsServerSettings parsed;
  if (!rain.valid() || !military.valid() || !rare.valid() || !low.valid() ||
      !circling.valid() || !warnings.valid() || !quiet.valid() ||
      !readSwitch(rain, parsed.rain) || !readSwitch(military, parsed.military) ||
      !readSwitch(rare, parsed.rare) || !readSwitch(low, parsed.low) ||
      !readSwitch(circling, parsed.circling) ||
      !readSwitch(warnings, parsed.warnings) ||
      !readInteger(rain, "lead", 10, 60, lead) ||
      !readInteger(rain, "dbz", 4, 60, dbz) ||
      !readInteger(rain, "radius", 1, 30, rainRadius) ||
      !readInteger(military, "radius", 1, 255, militaryRadius) ||
      !readInteger(rare, "radius", 1, 255, rareRadius) ||
      !readInteger(low, "distance", 1, 65535, distance) ||
      !readInteger(low, "height", 1, 65535, height) ||
      !readInteger(circling, "radius", 1, 255, circlingRadius) ||
      !readInteger(warnings, "level", 2, 4, level) ||
      !readInteger(quiet, "from", 0, 23, from) ||
      !readInteger(quiet, "to", 0, 23, to))
    return false;
  parsed.rainLeadMinutes = static_cast<uint8_t>(lead);
  parsed.rainMinimumDbz = static_cast<uint8_t>(dbz);
  parsed.rainRadiusKm = static_cast<uint8_t>(rainRadius);
  parsed.militaryRadiusKm = static_cast<uint8_t>(militaryRadius);
  parsed.rareRadiusKm = static_cast<uint8_t>(rareRadius);
  parsed.lowDistanceM = static_cast<uint16_t>(distance);
  parsed.lowHeightM = static_cast<uint16_t>(height);
  parsed.circlingRadiusKm = static_cast<uint8_t>(circlingRadius);
  parsed.warningsLevel = static_cast<uint8_t>(level);
  parsed.quietFromHour = static_cast<uint8_t>(from);
  parsed.quietToHour = static_cast<uint8_t>(to);
  parsed.known = true;
  parsed.elevationKnown = out.elevationKnown;
  parsed.elevationM = out.elevationM;
  memcpy(parsed.portal, out.portal, sizeof(parsed.portal));
  out = parsed;
  return true;
}
