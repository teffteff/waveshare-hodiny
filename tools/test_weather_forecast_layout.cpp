#include <cassert>
#include <cstdio>

#include "WeatherForecastLayout.h"

namespace {

// To, kvůli čemu se kvalita ovzduší vůbec dá vypnout: sekce zabírá tři řádky,
// které jinak dostanou hodiny. Se třemi dny je to šest hodin proti devíti.
void testAirQualityTradesRowsForHours() {
  assert(weatherForecastHourCapacity(true, 3) == 6);
  assert(weatherForecastHourCapacity(false, 3) == 9);
}

// Každý ubraný den je jedna hodina navíc, dokud se nenarazí na strop pole.
void testFewerDaysMeanMoreHours() {
  for (uint8_t days = 1; days <= WEATHER_FORECAST_MAX_DAYS; ++days) {
    assert(weatherForecastHourCapacity(true, days) <=
           weatherForecastHourCapacity(true, days - 1));
    assert(weatherForecastHourCapacity(false, days) <=
           weatherForecastHourCapacity(false, days - 1));
  }
  assert(weatherForecastHourCapacity(false, 4) == 8);
  assert(weatherForecastHourCapacity(true, 4) == 5);
}

void testStaysWithinBounds() {
  for (uint8_t air = 0; air <= 1; ++air) {
    for (uint8_t days = 0; days <= WEATHER_FORECAST_MAX_DAYS + 3; ++days) {
      const uint8_t hours = weatherForecastHourCapacity(air != 0, days);
      assert(hours >= 1);
      assert(hours <= WEATHER_FORECAST_MAX_HOURS);
    }
  }
  // Víc dní, než kolik jich odpověď nese, se ořízne na strop pole - jinak by
  // rozpočet ukrojil místo řádkům, které se nikdy nenakreslí.
  assert(weatherForecastHourCapacity(true, 9) ==
         weatherForecastHourCapacity(true, WEATHER_FORECAST_MAX_DAYS));
}

// Řádky se nesmí překrývat a poslední z nich musí zůstat nad hranicí, za
// kterou už kruhový displej řádek ořízne.
void testRowsFitTheCircle() {
  for (uint8_t air = 0; air <= 1; ++air) {
    for (uint8_t days = 0; days <= WEATHER_FORECAST_MAX_DAYS; ++days) {
      const uint8_t hours = weatherForecastHourCapacity(air != 0, days);
      const uint8_t rows = static_cast<uint8_t>(hours + days);
      int previous = WEATHER_FORECAST_ROWS_TOP_Y - WEATHER_FORECAST_ROW_HEIGHT;
      for (uint8_t index = 0; index < rows; ++index) {
        const int y = weatherForecastRowY(index, hours);
        assert(y - previous >= WEATHER_FORECAST_ROW_HEIGHT);
        previous = y;
      }
      assert(previous <= weatherForecastRowsBottomY(air != 0));
    }
  }
}

// Sekce s kvalitou ovzduší musí začínat pod posledním řádkem, jinak by se
// s předpovědí překryla.
void testAirQualitySitsBelowTheRows() {
  for (uint8_t days = 0; days <= WEATHER_FORECAST_MAX_DAYS; ++days) {
    const uint8_t hours = weatherForecastHourCapacity(true, days);
    const int lastRow =
        weatherForecastRowY(static_cast<uint8_t>(hours + days - 1), hours);
    assert(lastRow + WEATHER_FORECAST_ROW_HEIGHT / 2 <=
           WEATHER_FORECAST_AIR_TOP_Y - WEATHER_FORECAST_AIR_LINE_HEIGHT / 2);
  }
}

}  // namespace

int main() {
  testAirQualityTradesRowsForHours();
  testFewerDaysMeanMoreHours();
  testStaysWithinBounds();
  testRowsFitTheCircle();
  testAirQualitySitsBelowTheRows();
  printf("weather forecast layout: OK\n");
  return 0;
}
