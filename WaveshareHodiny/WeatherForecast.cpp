#include "WeatherForecast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

// Konec objektu, který začíná na `begin` znakem '{'. Počítá závorky, protože
// uvnitř "hourly" i "daily" jsou jen pole čísel - řetězec se závorkou by musel
// přijít z názvu časového pásma, a ten leží v hlavičce odpovědi, ne tady.
const char *objectEnd(const char *begin) {
  if (begin == nullptr || *begin != '{') return nullptr;
  int depth = 0;
  for (const char *cursor = begin; *cursor != '\0'; ++cursor) {
    if (*cursor == '{') ++depth;
    else if (*cursor == '}' && --depth == 0) return cursor;
  }
  return nullptr;
}

// Najde pojmenovaný objekt a vrátí jeho vnitřek. Hledá se celý klíč
// i s uvozovkami, takže "hourly" nesebere "hourly_units".
bool findObject(const char *payload, const char *key, const char *&begin,
                const char *&end) {
  begin = nullptr;
  end = nullptr;
  if (payload == nullptr) return false;
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":{", key);
  const char *found = strstr(payload, pattern);
  if (found == nullptr) return false;
  const char *objectBegin = found + strlen(pattern) - 1;
  const char *objectEndPosition = objectEnd(objectBegin);
  if (objectEndPosition == nullptr) return false;
  begin = objectBegin;
  end = objectEndPosition;
  return true;
}

// Průchod jedním polem čísel. Drží si jen ukazatel, takže se všechna pole dají
// číst po prvcích současně a odpověď se nikam nekopíruje.
struct ArrayCursor {
  const char *cursor = nullptr;
  const char *end = nullptr;

  bool valid() const { return cursor != nullptr; }
};

ArrayCursor openArray(const char *sectionBegin, const char *sectionEnd,
                      const char *key) {
  ArrayCursor result;
  if (sectionBegin == nullptr || sectionEnd == nullptr) return result;
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":[", key);
  const char *found = strstr(sectionBegin, pattern);
  if (found == nullptr || found >= sectionEnd) return result;
  const char *arrayBegin = found + strlen(pattern);
  const char *arrayEnd = strchr(arrayBegin, ']');
  if (arrayEnd == nullptr || arrayEnd > sectionEnd) return result;
  result.cursor = arrayBegin;
  result.end = arrayEnd;
  return result;
}

// Další prvek pole. Vrací false na konci pole; `null` je platný prvek, jen bez
// hodnoty - Open-Meteo ho posílá u veličin, které pro danou hodinu nemá.
bool nextValue(ArrayCursor &array, double &value) {
  value = NAN;
  if (!array.valid()) return false;
  const char *cursor = array.cursor;
  while (cursor < array.end &&
         (*cursor == ' ' || *cursor == ',' || *cursor == '\n' ||
          *cursor == '\r' || *cursor == '\t')) {
    ++cursor;
  }
  if (cursor >= array.end) {
    array.cursor = nullptr;
    return false;
  }
  if (*cursor == 'n') {
    array.cursor = cursor + 4;
    return true;
  }
  char *parseEnd = nullptr;
  const double parsed = strtod(cursor, &parseEnd);
  if (parseEnd == nullptr || parseEnd == cursor) {
    array.cursor = nullptr;
    return false;
  }
  value = parsed;
  array.cursor = parseEnd;
  return true;
}

float toFloat(double value) { return static_cast<float>(value); }

int toWeatherCode(double value) {
  return isfinite(value) ? static_cast<int>(lround(value)) : -1;
}

// Skalární hodnota uvnitř objektu. Používá se jen pro kvalitu ovzduší, kde
// odpověď nese v "current" jedno číslo od každé veličiny.
float scalarValue(const char *sectionBegin, const char *sectionEnd,
                  const char *key) {
  if (sectionBegin == nullptr || sectionEnd == nullptr) return NAN;
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char *found = strstr(sectionBegin, pattern);
  if (found == nullptr || found >= sectionEnd) return NAN;
  const char *value = found + strlen(pattern);
  if (*value == 'n') return NAN;
  char *parseEnd = nullptr;
  const double parsed = strtod(value, &parseEnd);
  if (parseEnd == value) return NAN;
  return static_cast<float>(parsed);
}

}  // namespace

bool weatherForecastParse(const char *payload, int64_t fromTime,
                          int64_t fromDay, size_t wantedHours,
                          size_t wantedDays, WeatherForecastData &forecast) {
  forecast = WeatherForecastData{};
  if (payload == nullptr) return false;
  if (wantedHours > WEATHER_FORECAST_MAX_HOURS)
    wantedHours = WEATHER_FORECAST_MAX_HOURS;
  if (wantedDays > WEATHER_FORECAST_MAX_DAYS)
    wantedDays = WEATHER_FORECAST_MAX_DAYS;

  const char *hourlyBegin = nullptr;
  const char *hourlyEnd = nullptr;
  if (findObject(payload, "hourly", hourlyBegin, hourlyEnd)) {
    ArrayCursor time = openArray(hourlyBegin, hourlyEnd, "time");
    ArrayCursor temperature =
        openArray(hourlyBegin, hourlyEnd, "temperature_2m");
    ArrayCursor precipitation =
        openArray(hourlyBegin, hourlyEnd, "precipitation");
    ArrayCursor code = openArray(hourlyBegin, hourlyEnd, "weather_code");
    ArrayCursor wind = openArray(hourlyBegin, hourlyEnd, "wind_speed_10m");
    ArrayCursor day = openArray(hourlyBegin, hourlyEnd, "is_day");
    double stamp = 0;
    while (forecast.hourCount < wantedHours && nextValue(time, stamp)) {
      double temperatureValue = NAN;
      double precipitationValue = NAN;
      double codeValue = NAN;
      double windValue = NAN;
      double dayValue = NAN;
      nextValue(temperature, temperatureValue);
      nextValue(precipitation, precipitationValue);
      nextValue(code, codeValue);
      nextValue(wind, windValue);
      nextValue(day, dayValue);
      if (!isfinite(stamp)) continue;
      const int64_t hourTime = static_cast<int64_t>(stamp);
      if (hourTime < fromTime) continue;
      WeatherForecastHour &hour = forecast.hours[forecast.hourCount++];
      hour.time = hourTime;
      hour.temperatureC = toFloat(temperatureValue);
      hour.precipitationMm = toFloat(precipitationValue);
      hour.windKmh = toFloat(windValue);
      hour.weatherCode = toWeatherCode(codeValue);
      // Bez is_day v odpovědi je denní ikona menší chyba než noční v poledne.
      hour.isDay = !isfinite(dayValue) || dayValue >= 0.5;
    }
  }

  const char *dailyBegin = nullptr;
  const char *dailyEnd = nullptr;
  if (findObject(payload, "daily", dailyBegin, dailyEnd)) {
    ArrayCursor time = openArray(dailyBegin, dailyEnd, "time");
    ArrayCursor code = openArray(dailyBegin, dailyEnd, "weather_code");
    ArrayCursor maximum = openArray(dailyBegin, dailyEnd, "temperature_2m_max");
    ArrayCursor minimum = openArray(dailyBegin, dailyEnd, "temperature_2m_min");
    ArrayCursor precipitation =
        openArray(dailyBegin, dailyEnd, "precipitation_sum");
    ArrayCursor wind = openArray(dailyBegin, dailyEnd, "wind_speed_10m_max");
    double stamp = 0;
    while (forecast.dayCount < wantedDays && nextValue(time, stamp)) {
      double codeValue = NAN;
      double maximumValue = NAN;
      double minimumValue = NAN;
      double precipitationValue = NAN;
      double windValue = NAN;
      nextValue(code, codeValue);
      nextValue(maximum, maximumValue);
      nextValue(minimum, minimumValue);
      nextValue(precipitation, precipitationValue);
      nextValue(wind, windValue);
      if (!isfinite(stamp)) continue;
      const int64_t dayTime = static_cast<int64_t>(stamp);
      if (dayTime < fromDay) continue;
      WeatherForecastDay &day = forecast.days[forecast.dayCount++];
      day.time = dayTime;
      day.weatherCode = toWeatherCode(codeValue);
      day.maximumC = toFloat(maximumValue);
      day.minimumC = toFloat(minimumValue);
      day.precipitationMm = toFloat(precipitationValue);
      day.windKmh = toFloat(windValue);
    }
  }

  return forecast.hourCount > 0;
}

bool weatherForecastParseAirQuality(const char *payload,
                                    WeatherForecastAirQuality &air) {
  air = WeatherForecastAirQuality{};
  if (payload == nullptr) return false;
  const char *begin = nullptr;
  const char *end = nullptr;
  // Jednotky leží v "current_units", takže se hledá celý klíč i s dvojtečkou.
  if (!findObject(payload, "current", begin, end)) return false;
  air.europeanAqi = scalarValue(begin, end, "european_aqi");
  air.pm25 = scalarValue(begin, end, "pm2_5");
  air.grassPollen = scalarValue(begin, end, "grass_pollen");
  return isfinite(air.europeanAqi) || isfinite(air.pm25) ||
         isfinite(air.grassPollen);
}
