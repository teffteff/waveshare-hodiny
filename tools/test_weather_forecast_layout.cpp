#include <cassert>
#include <cstdio>

#include "WeatherForecastLayout.h"

namespace {

// Celý týden se na druhou stránku vejde nad kvalitu ovzduší a řádky se
// nepřekrývají ani s ní.
void testWeekFitsAboveAirQuality() {
  int previous = WEATHER_FORECAST_ROWS_TOP_Y - WEATHER_FORECAST_DAY_ROW_HEIGHT;
  for (uint8_t index = 0; index < WEATHER_FORECAST_MAX_DAYS; ++index) {
    const int y = weatherForecastDayRowY(index);
    assert(y - previous >= WEATHER_FORECAST_DAY_ROW_HEIGHT);
    previous = y;
  }
  const int lastDayBottom = previous + WEATHER_FORECAST_DAY_ROW_HEIGHT / 2;
  const int firstAirTop =
      WEATHER_FORECAST_AIR_TOP_Y - WEATHER_FORECAST_AIR_LINE_HEIGHT / 2;
  // Mezi nimi musí zbýt místo na dělicí čáru.
  assert(firstAirTop - lastDayBottom >= 8);
}

// Poslední řádek kvality ovzduší končí tam, kde pás řádků, aby ho kruh
// neořízl.
void testAirQualityEndsWithTheBand() {
  const int lastAir =
      WEATHER_FORECAST_AIR_TOP_Y +
      (WEATHER_FORECAST_AIR_LINE_COUNT - 1) * WEATHER_FORECAST_AIR_LINE_HEIGHT;
  assert(lastAir == WEATHER_FORECAST_ROWS_BOTTOM_Y);
}

// Graf zabírá celý pás řádků první stránky.
void testChartCoversTheBand() {
  assert(WEATHER_FORECAST_CHART_TOP_Y ==
         WEATHER_FORECAST_ROWS_TOP_Y - WEATHER_FORECAST_ROW_HEIGHT / 2);
  assert(WEATHER_FORECAST_CHART_BOTTOM_Y ==
         WEATHER_FORECAST_ROWS_BOTTOM_Y + WEATHER_FORECAST_ROW_HEIGHT / 2);
}

}  // namespace

int main() {
  testWeekFitsAboveAirQuality();
  testAirQualityEndsWithTheBand();
  testChartCoversTheBand();
  printf("weather forecast layout: OK\n");
  return 0;
}
