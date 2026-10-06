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

inline WeatherForecastChartRange weatherForecastChartRange(
    const WeatherForecastHour *hours, size_t count) {
  WeatherForecastChartRange range;
  for (size_t index = 0; index < count; ++index) {
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
