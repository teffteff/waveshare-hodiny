#include "RainAlert.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "JsonScan.h"

namespace {

// Celé číslo se znaménkem z JSON hodnoty. Odrazivost je malé celé číslo, ale
// -1 znamená "snímek chybí", takže se znaménko nesmí ztratit.
bool readInteger(const JsonValue &value, int16_t &out) {
  float number = 0.0f;
  if (!jsonReadNumber(value, number)) return false;
  if (!std::isfinite(number)) return false;
  const long rounded = lroundf(number);
  if (rounded < -1 || rounded > 100) return false;
  out = static_cast<int16_t>(rounded);
  return true;
}

// Unixový čas z odpovědi. Do floatu by se nevešel bez ztráty sekund, proto se
// čte po číslicích.
bool readEpoch(const JsonValue &value, uint32_t &out) {
  if (!value.valid() || value.isString) return false;
  const char *cursor = value.contentBegin();
  const char *end = value.contentEnd();
  uint64_t result = 0;
  bool digits = false;
  for (; cursor < end && *cursor >= '0' && *cursor <= '9'; ++cursor) {
    result = result * 10 + static_cast<uint64_t>(*cursor - '0');
    if (result > 0xFFFFFFFFULL) return false;
    digits = true;
  }
  // Server posílá celé sekundy, ale kdyby přidal desetinnou část, zahodí se.
  if (cursor < end && *cursor == '.') {
    ++cursor;
    while (cursor < end && *cursor >= '0' && *cursor <= '9') ++cursor;
  }
  if (!digits || cursor != end) return false;
  out = static_cast<uint32_t>(result);
  return true;
}

}  // namespace

bool rainForecastParse(const char *begin, const char *end,
                       RainForecast &forecast) {
  forecast = RainForecast{};
  if (begin == nullptr || end == nullptr || begin >= end) return false;
  const char *objectBegin = jsonSkipWhitespace(begin, end);
  if (objectBegin >= end || *objectBegin != '{') return false;
  const char *objectEnd = jsonValueEnd(objectBegin, end);
  if (objectEnd == nullptr) return false;

  // "now" musí dorazit: bez něj se nedá poznat, jestli prší už teď, a celá
  // odpověď je podezřelá.
  const JsonValue nowValue = jsonFindMember(objectBegin, objectEnd, "now");
  if (!readInteger(nowValue, forecast.now)) return false;

  forecast.covered = jsonReadBoolMember(objectBegin, objectEnd, "covered");
  readEpoch(jsonFindMember(objectBegin, objectEnd, "slot"), forecast.slot);
  readEpoch(jsonFindMember(objectBegin, objectEnd, "time"), forecast.serverTime);

  float step = 0.0f;
  if (jsonReadNumberMember(objectBegin, objectEnd, "step", step)) {
    const long rounded = lroundf(step);
    if (rounded >= 1 && rounded <= 60)
      forecast.stepMinutes = static_cast<uint8_t>(rounded);
  }

  const JsonValue wide = jsonFindMember(objectBegin, objectEnd, "wide");
  if (!readInteger(wide, forecast.wide)) forecast.wide = -1;

  const JsonValue steps = jsonFindMember(objectBegin, objectEnd, "steps");
  JsonArrayCursor cursor = jsonOpenArray(steps);
  while (jsonNextItem(cursor) && forecast.stepCount < RAIN_FORECAST_MAX_STEPS) {
    JsonValue item;
    item.begin = cursor.itemBegin;
    item.end = cursor.itemEnd;
    int16_t value = 0;
    // Nečitelný prvek se počítá za chybějící snímek, ne za sucho.
    if (!readInteger(item, value)) value = -1;
    forecast.steps[forecast.stepCount++] = value;
  }
  return true;
}

bool rainFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                      uint8_t radiusKm, char *output, size_t capacity) {
  if (baseUrl == nullptr || baseUrl[0] == '\0' || output == nullptr ||
      capacity == 0)
    return false;
  const char separator = strchr(baseUrl, '?') != nullptr ? '&' : '?';
  const int written =
      snprintf(output, capacity, "%s%clat=%.5f&lon=%.5f&r=%u&w=%u", baseUrl,
               separator, latitude, longitude, static_cast<unsigned>(radiusKm),
               static_cast<unsigned>(RAIN_WIDE_RADIUS_KM));
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool rainAlertEvaluate(const RainForecast &forecast, uint8_t minimumDbz,
                       uint8_t horizonMinutes, RainAlertDecision &decision) {
  decision = RainAlertDecision{};
  // Mimo dosah radaru je prázdno slepé místo, ne sucho.
  if (!forecast.covered) return false;
  const int16_t threshold = static_cast<int16_t>(minimumDbz);
  decision.rainingNow = forecast.now >= threshold;
  // Upozornění je na příchozí déšť. Když prší už teď, není co hlásit.
  if (decision.rainingNow) return false;
  if (forecast.stepMinutes == 0) return false;

  for (size_t index = 0; index < forecast.stepCount; ++index) {
    const uint16_t minutes =
        static_cast<uint16_t>((index + 1) * forecast.stepMinutes);
    if (minutes > horizonMinutes) break;
    // Chybějící snímek se přeskočí; nedá se z něj udělat závěr ani tak, ani tak.
    if (forecast.steps[index] < 0) continue;
    if (forecast.steps[index] < threshold) continue;
    decision.raise = true;
    decision.minutesAway = minutes;
    decision.dbz = forecast.steps[index];
    return true;
  }
  return false;
}

bool rainForecastWideRain(const RainForecast &forecast, uint8_t minimumDbz) {
  return forecast.covered && forecast.wide >= static_cast<int16_t>(minimumDbz);
}
