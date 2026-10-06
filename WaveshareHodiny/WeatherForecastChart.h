#pragma once

#include <math.h>
#include <stddef.h>

#include "WeatherForecast.h"

// Měřítka grafu příštích hodin, oddělená od kreslení, aby šla testovat na
// počítači. Graf kreslí teplotu čarou a srážky sloupci, stejně jako graf
// "Další hodiny" na nástěnce.

// Nejmenší rozpětí teplotní osy. Den, kdy se teplota hne o stupeň, by se
// jinak roztáhl přes celý graf a vypadal jako prudká změna.
constexpr float WEATHER_FORECAST_CHART_MIN_SPAN_C = 6.0f;
constexpr float WEATHER_FORECAST_CHART_AXIS_PADDING_C = 1.0f;
// Nejmenší strop srážkové osy: mrholení 0,2 mm nemá vypadat jako průtrž.
constexpr float WEATHER_FORECAST_CHART_MIN_RAIN_MM = 2.0f;
// Srážky pod desetinu milimetru nejsou déšť, jen vlhko ve vzduchu - stejná
// hranice, pod kterou seznam nechával sloupec prázdný.
constexpr float WEATHER_FORECAST_CHART_RAIN_FLOOR_MM = 0.05f;

struct WeatherForecastChartRange {
  // Nejnižší a nejvyšší hodina; -1, když žádná hodina teplotu nemá.
  int lowIndex = -1;
  int highIndex = -1;
  // Teplotní osa po rozšíření na nejmenší rozpětí.
  float axisLowC = NAN;
  float axisHighC = NAN;
  // Nejdeštivější hodina; -1, když nikde neprší.
  int wettestIndex = -1;
  float rainScaleMm = WEATHER_FORECAST_CHART_MIN_RAIN_MM;
};

// Maximum, minimum a nejdeštivější hodina se hledají od `firstIndex`: hodiny
// před ním už uplynuly a popisky mají patřit k tomu, co teprve přijde.
inline WeatherForecastChartRange weatherForecastChartRange(
    const WeatherForecastHour *hours, size_t count, size_t firstIndex = 0) {
  WeatherForecastChartRange range;
  for (size_t index = firstIndex; index < count; ++index) {
    const float temperature = hours[index].temperatureC;
    if (!isnan(temperature)) {
      if (range.lowIndex < 0 ||
          temperature < hours[range.lowIndex].temperatureC)
        range.lowIndex = static_cast<int>(index);
      if (range.highIndex < 0 ||
          temperature > hours[range.highIndex].temperatureC)
        range.highIndex = static_cast<int>(index);
    }
    const float rain = hours[index].precipitationMm;
    if (!isnan(rain) && rain >= WEATHER_FORECAST_CHART_RAIN_FLOOR_MM &&
        (range.wettestIndex < 0 ||
         rain > hours[range.wettestIndex].precipitationMm))
      range.wettestIndex = static_cast<int>(index);
  }
  if (range.lowIndex >= 0) {
    float low = hours[range.lowIndex].temperatureC;
    float high = hours[range.highIndex].temperatureC;
    const float missing = WEATHER_FORECAST_CHART_MIN_SPAN_C - (high - low);
    if (missing > 0.0f) {
      low -= missing / 2.0f;
      high += missing / 2.0f;
    }
    // Stupeň navíc na obou koncích, jako na nástěnce: den mezi 5,3 a 20
    // stupni tak dostane i čáru pěti stupňů pod nejnižší hodinou.
    low -= WEATHER_FORECAST_CHART_AXIS_PADDING_C;
    high += WEATHER_FORECAST_CHART_AXIS_PADDING_C;
    range.axisLowC = low;
    range.axisHighC = high;
  }
  if (range.wettestIndex >= 0 &&
      hours[range.wettestIndex].precipitationMm > range.rainScaleMm)
    range.rainScaleMm = hours[range.wettestIndex].precipitationMm;
  return range;
}

// Svislá poloha teploty mezi `top` (nejvyšší hodnota osy) a `bottom`.
inline float weatherForecastChartTemperatureY(
    const WeatherForecastChartRange &range, float temperature, float top,
    float bottom) {
  const float span = range.axisHighC - range.axisLowC;
  if (!(span > 0.0f)) return (top + bottom) / 2.0f;
  return bottom - (temperature - range.axisLowC) / span * (bottom - top);
}

// Výška srážkového sloupce v pixelech, nanejvýš `maximumHeight`. Slabé
// srážky dostanou aspoň dva pixely, aby nezmizely.
inline int weatherForecastChartRainHeight(
    const WeatherForecastChartRange &range, float millimeters,
    int maximumHeight) {
  if (isnan(millimeters) || millimeters < WEATHER_FORECAST_CHART_RAIN_FLOOR_MM)
    return 0;
  int height = static_cast<int>(
      lroundf(millimeters / range.rainScaleMm * maximumHeight));
  if (height < 2) height = 2;
  return height > maximumHeight ? maximumHeight : height;
}

// Po kolika hodinách psát popisky a ikony, aby od sebe byly aspoň
// `minimumSpacing` pixelů. Kroky dělí den beze zbytku, takže popisky padnou
// na stejné hodiny každý den.
inline int weatherForecastChartLabelStep(float columnWidth,
                                         float minimumSpacing) {
  static const int STEPS[] = {1, 2, 3, 4, 6, 12};
  for (int step : STEPS) {
    if (step * columnWidth >= minimumSpacing) return step;
  }
  return 12;
}

// Krok vodorovných čar teplotní osy: 1, 2, 5 nebo 10 stupňů tak, aby jich na
// ose bylo nanejvýš `maximumLines` - stejně jako na nástěnce.
inline int weatherForecastChartGridStep(float low, float high,
                                        int maximumLines) {
  static const int STEPS[] = {1, 2, 5, 10};
  for (int step : STEPS) {
    const int lines = static_cast<int>(floorf(high / step)) -
                      static_cast<int>(ceilf(low / step)) + 1;
    if (lines <= maximumLines) return step;
  }
  return 20;
}

// Kde na ose hodin leží `now`: 0 je první hodina grafu, 1 druhá a tak dál,
// mezi nimi zlomek. Mimo graf se ořízne na jeho okraj. Vrací false, když
// graf nemá ani jednu hodinu.
inline bool weatherForecastChartNowPosition(const WeatherForecastHour *hours,
                                            size_t count, int64_t now,
                                            float &position) {
  if (count == 0) return false;
  position = static_cast<float>(now - hours[0].time) / 3600.0f;
  if (position < 0.0f) position = 0.0f;
  const float last = static_cast<float>(count - 1);
  if (position > last) position = last;
  return true;
}

// Teplota v místě `position` lineárně mezi sousedními hodinami, nebo NAN,
// když jedna z nich teplotu nemá.
inline float weatherForecastChartTemperatureAt(
    const WeatherForecastHour *hours, size_t count, float position) {
  if (count == 0) return NAN;
  size_t index = static_cast<size_t>(position);
  if (index + 1 >= count) return hours[count - 1].temperatureC;
  const float fraction = position - static_cast<float>(index);
  const float first = hours[index].temperatureC;
  const float second = hours[index + 1].temperatureC;
  if (isnan(first) || isnan(second)) return NAN;
  return first + (second - first) * fraction;
}

// Rozšíří teplotní osu tak, aby se na ni vešla i `valueC` - třeba naměřená
// teplota z uplynulých hodin - se stejnou rezervou jako předpověď.
inline void weatherForecastChartExtendAxis(WeatherForecastChartRange &range,
                                           float valueC) {
  if (isnan(valueC)) return;
  const float low = valueC - WEATHER_FORECAST_CHART_AXIS_PADDING_C;
  const float high = valueC + WEATHER_FORECAST_CHART_AXIS_PADDING_C;
  if (isnan(range.axisLowC) || low < range.axisLowC) range.axisLowC = low;
  if (isnan(range.axisHighC) || high > range.axisHighC) range.axisHighC = high;
}
