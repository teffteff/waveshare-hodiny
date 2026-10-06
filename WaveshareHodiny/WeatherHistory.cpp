#include "WeatherHistory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "JsonScan.h"

namespace {

// Celé číslo bez ztráty přesnosti: float by čas od epochy zaokrouhlil na
// desítky sekund.
bool readInt64(const JsonValue &value, int64_t &out) {
  if (!value.valid() || value.isString) return false;
  char text[24];
  const size_t length = static_cast<size_t>(value.end - value.begin);
  if (length == 0 || length >= sizeof(text)) return false;
  memcpy(text, value.begin, length);
  text[length] = '\0';
  char *stop = nullptr;
  const long long parsed = strtoll(text, &stop, 10);
  if (stop == text || *stop != '\0') return false;
  out = parsed;
  return true;
}

// Pole čísel do `values`; null nebo cokoli jiného je NAN. Vrací počet prvků.
size_t readNumbers(const JsonValue &array, float *values, size_t capacity) {
  if (!array.isArray()) return 0;
  JsonArrayCursor cursor = jsonOpenArray(array);
  size_t count = 0;
  while (count < capacity && jsonNextItem(cursor)) {
    JsonValue item;
    item.begin = cursor.itemBegin;
    item.end = cursor.itemEnd;
    float value = NAN;
    if (!jsonReadNumber(item, value)) value = NAN;
    values[count++] = value;
  }
  return count;
}

}  // namespace

bool weatherHistoryParse(const char *payload, size_t length,
                         WeatherHistoryData &data) {
  data = WeatherHistoryData{};
  if (payload == nullptr) return false;
  const char *end = payload + length;
  const char *begin = jsonSkipWhitespace(payload, end);
  const char *objectEnd = jsonValueEnd(begin, end);
  if (objectEnd == nullptr || begin >= objectEnd || *begin != '{') return false;

  if (!readInt64(jsonFindMember(begin, objectEnd, "start"), data.start))
    return false;
  readInt64(jsonFindMember(begin, objectEnd, "now"), data.now);

  const JsonValue temp = jsonFindMember(begin, objectEnd, "temp");
  if (temp.isObject()) {
    int64_t step = 0;
    readInt64(jsonFindMember(temp.begin, temp.end, "start"), data.lineStart);
    if (readInt64(jsonFindMember(temp.begin, temp.end, "step"), step) &&
        step > 0 && step <= 3600) {
      data.lineStep = static_cast<uint32_t>(step);
      data.lineCount = readNumbers(jsonFindMember(temp.begin, temp.end, "values"),
                                   data.line, WEATHER_HISTORY_MAX_LINE);
    }
  }

  float wind[WEATHER_HISTORY_HOURS];
  float rain[WEATHER_HISTORY_HOURS];
  float code[WEATHER_HISTORY_HOURS];
  float day[WEATHER_HISTORY_HOURS];
  const size_t windCount = readNumbers(jsonFindMember(begin, objectEnd, "wind"),
                                       wind, WEATHER_HISTORY_HOURS);
  const size_t rainCount = readNumbers(jsonFindMember(begin, objectEnd, "rain"),
                                       rain, WEATHER_HISTORY_HOURS);
  const size_t codeCount = readNumbers(jsonFindMember(begin, objectEnd, "code"),
                                       code, WEATHER_HISTORY_HOURS);
  const size_t dayCount = readNumbers(jsonFindMember(begin, objectEnd, "day"),
                                      day, WEATHER_HISTORY_HOURS);
  size_t hours = windCount;
  if (rainCount > hours) hours = rainCount;
  if (codeCount > hours) hours = codeCount;
  if (dayCount > hours) hours = dayCount;
  data.hourCount = hours;
  for (size_t index = 0; index < hours; ++index) {
    WeatherForecastHour &hour = data.hours[index];
    hour.time = data.start + static_cast<int64_t>(index) * 3600;
    hour.temperatureC = weatherHistoryLineAt(data, hour.time);
    hour.windKmh = index < windCount ? wind[index] : NAN;
    hour.precipitationMm = index < rainCount ? rain[index] : NAN;
    hour.weatherCode = index < codeCount && !isnan(code[index])
                           ? static_cast<int>(lroundf(code[index]))
                           : -1;
    // Bez údaje je den menší chyba než noc v poledne, jako u předpovědi.
    hour.isDay = !(index < dayCount && !isnan(day[index]) && day[index] < 0.5f);
  }
  return data.hourCount > 0 || data.lineCount > 0;
}

namespace {
// Má adresa v dotazu parametr `name`?
bool hasQueryParameter(const char *url, const char *name) {
  const char *query = strchr(url, '?');
  if (query == nullptr) return false;
  const size_t length = strlen(name);
  for (const char *cursor = query; cursor != nullptr;
       cursor = strchr(cursor + 1, '&')) {
    if (strncmp(cursor + 1, name, length) == 0 && cursor[1 + length] == '=')
      return true;
  }
  return false;
}

// Entita smí obsahovat jen znaky, které HA v id používá; nic z toho se
// v adrese nekóduje.
bool plainEntity(const char *entity) {
  for (const char *cursor = entity; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '.'))
      return false;
  }
  return true;
}
}  // namespace

bool weatherHistoryBuildUrl(const char *base, const char *temperatureEntity,
                            char *url, size_t capacity) {
  if (base == nullptr || url == nullptr || capacity == 0) return false;
  int written = 0;
  if (temperatureEntity != nullptr && temperatureEntity[0] != '\0' &&
      plainEntity(temperatureEntity) && !hasQueryParameter(base, "temp") &&
      !hasQueryParameter(base, "temp_entity")) {
    written = snprintf(url, capacity, "%s%ctemp_entity=%s", base,
                       strchr(base, '?') != nullptr ? '&' : '?',
                       temperatureEntity);
  } else {
    written = snprintf(url, capacity, "%s", base);
  }
  return written >= 0 && static_cast<size_t>(written) < capacity;
}

float weatherHistoryLineAt(const WeatherHistoryData &data, int64_t time) {
  if (data.lineCount == 0 || time < data.lineStart) return NAN;
  const int64_t offset = time - data.lineStart;
  const size_t index = static_cast<size_t>(offset / data.lineStep);
  if (index >= data.lineCount) return NAN;
  const float first = data.line[index];
  if (index + 1 >= data.lineCount) return offset % data.lineStep == 0 ? first : NAN;
  const float second = data.line[index + 1];
  const float fraction =
      static_cast<float>(offset % data.lineStep) / static_cast<float>(data.lineStep);
  if (fraction == 0.0f) return first;
  if (isnan(first) || isnan(second)) return NAN;
  return first + (second - first) * fraction;
}
