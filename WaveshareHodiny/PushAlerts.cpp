#include "PushAlerts.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {
bool nameCharacter(char value) {
  return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
         value == '-';
}

const char *flag(bool value) { return value ? "true" : "false"; }
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

bool pushAlertsBuildUrl(const char *base, const char *id, char *url,
                        size_t capacity) {
  if (base == nullptr || id == nullptr || base[0] == '\0' || id[0] == '\0')
    return false;
  size_t baseLength = strlen(base);
  while (baseLength > 0 && base[baseLength - 1] == '/') --baseLength;
  const int written = snprintf(url, capacity, "%.*s/config/%s",
                               static_cast<int>(baseLength), base, id);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool pushAlertsBuildBody(const ClockPushAlertsConfig &alerts, float latitude,
                         float longitude, const char *name, char *body,
                         size_t capacity) {
  const char *label = name != nullptr ? name : "";
  for (const char *cursor = label; *cursor != '\0'; ++cursor) {
    if (!nameCharacter(*cursor)) return false;
  }
  if (strlen(label) > 32) return false;
  const int written = snprintf(
      body, capacity,
      "{\"name\":\"%s\",\"enabled\":%s,\"lat\":%.5f,\"lon\":%.5f,"
      "\"rain\":{\"enabled\":%s,\"lead\":%u,\"dbz\":%u,\"radius\":%u},"
      "\"planes\":{\"military\":%s,\"rare\":%s,\"low\":%s,\"radius\":%u,"
      "\"lowRadius\":%u,\"lowHeight\":%u},"
      "\"quiet\":{\"from\":%u,\"to\":%u}}",
      label, flag(alerts.enabled), static_cast<double>(latitude),
      static_cast<double>(longitude), flag(alerts.rain),
      static_cast<unsigned>(alerts.rainLeadMinutes),
      static_cast<unsigned>(alerts.rainMinimumDbz),
      static_cast<unsigned>(alerts.rainRadiusKm), flag(alerts.military),
      flag(alerts.rare), flag(alerts.low),
      static_cast<unsigned>(alerts.watchRadiusKm),
      static_cast<unsigned>(alerts.lowRadiusM),
      static_cast<unsigned>(alerts.lowHeightM),
      static_cast<unsigned>(alerts.quietFromHour),
      static_cast<unsigned>(alerts.quietToHour));
  return written > 0 && static_cast<size_t>(written) < capacity;
}
