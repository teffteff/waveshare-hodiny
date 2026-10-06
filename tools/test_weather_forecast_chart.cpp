#include <cassert>
#include <cmath>
#include <cstdio>

#include "WeatherForecastChart.h"

namespace {

WeatherForecastHour hour(float temperature, float rain) {
  WeatherForecastHour result;
  result.temperatureC = temperature;
  result.precipitationMm = rain;
  return result;
}

// Popisky min a max patří ke skutečným hodinám, osa se kvůli nim nenatahuje.
void testFindsExtremes() {
  const WeatherForecastHour hours[] = {hour(12, 0), hour(8, 0), hour(19, 0),
                                       hour(15, 0)};
  const WeatherForecastChartRange range = weatherForecastChartRange(hours, 4);
  assert(range.lowIndex == 1);
  assert(range.highIndex == 2);
  // Osa má stupeň navíc na obou koncích.
  assert(range.axisLowC == 7.0f);
  assert(range.axisHighC == 20.0f);
  assert(weatherForecastChartTemperatureY(range, 20.0f, 10.0f, 140.0f) ==
         10.0f);
  assert(weatherForecastChartTemperatureY(range, 7.0f, 10.0f, 140.0f) ==
         140.0f);
}

// Den, kdy se teplota skoro nehne, zůstane na grafu skoro rovný.
void testNarrowRangeWidensAroundTheMiddle() {
  const WeatherForecastHour hours[] = {hour(10, 0), hour(11, 0)};
  const WeatherForecastChartRange range = weatherForecastChartRange(hours, 2);
  assert(std::fabs(range.axisHighC - range.axisLowC -
                   WEATHER_FORECAST_CHART_MIN_SPAN_C -
                   2 * WEATHER_FORECAST_CHART_AXIS_PADDING_C) < 0.001f);
  assert(std::fabs((range.axisHighC + range.axisLowC) / 2.0f - 10.5f) <
         0.001f);
}

// Chybějící teploty se přeskočí; bez jediné zůstane graf bez čáry.
void testMissingTemperatures() {
  const WeatherForecastHour hours[] = {hour(NAN, 0), hour(5, 0), hour(NAN, 0)};
  WeatherForecastChartRange range = weatherForecastChartRange(hours, 3);
  assert(range.lowIndex == 1 && range.highIndex == 1);
  const WeatherForecastHour empty[] = {hour(NAN, 0)};
  range = weatherForecastChartRange(empty, 1);
  assert(range.lowIndex < 0 && range.highIndex < 0);
}

// Mrholení nesmí vypadat jako průtrž: osa má strop aspoň dva milimetry,
// a vlhko pod desetinu milimetru se nekreslí vůbec.
void testRainScale() {
  const WeatherForecastHour drizzle[] = {hour(10, 0.2f), hour(10, 0.01f)};
  WeatherForecastChartRange range = weatherForecastChartRange(drizzle, 2);
  assert(range.wettestIndex == 0);
  assert(range.rainScaleMm == WEATHER_FORECAST_CHART_MIN_RAIN_MM);
  assert(weatherForecastChartRainHeight(range, 0.2f, 40) == 4);
  assert(weatherForecastChartRainHeight(range, 0.01f, 40) == 0);
  assert(weatherForecastChartRainHeight(range, 0.06f, 40) == 2);

  const WeatherForecastHour storm[] = {hour(10, 1.0f), hour(10, 8.0f)};
  range = weatherForecastChartRange(storm, 2);
  assert(range.wettestIndex == 1);
  assert(range.rainScaleMm == 8.0f);
  assert(weatherForecastChartRainHeight(range, 8.0f, 40) == 40);
  assert(weatherForecastChartRainHeight(range, 1.0f, 40) == 5);

  const WeatherForecastHour dry[] = {hour(10, 0), hour(10, NAN)};
  range = weatherForecastChartRange(dry, 2);
  assert(range.wettestIndex < 0);
}

void testLabelStep() {
  // 24 hodin na 288 px: popisky po třech hodinách.
  assert(weatherForecastChartLabelStep(12.0f, 36.0f) == 3);
  assert(weatherForecastChartLabelStep(40.0f, 36.0f) == 1);
  assert(weatherForecastChartLabelStep(10.0f, 36.0f) == 4);
  assert(weatherForecastChartLabelStep(1.0f, 36.0f) == 12);
}

// Osa jako na nástěnce: 5, 10, 15, 20 pro den mezi pěti a dvaceti stupni.
void testGridStep() {
  assert(weatherForecastChartGridStep(5.0f, 20.0f, 4) == 5);
  assert(weatherForecastChartGridStep(7.5f, 13.5f, 4) == 2);
  assert(weatherForecastChartGridStep(-3.0f, 0.0f, 4) == 1);
  assert(weatherForecastChartGridStep(-8.0f, 31.0f, 4) == 10);
  assert(weatherForecastChartGridStep(-40.0f, 40.0f, 4) == 20);
}

}  // namespace

int main() {
  testFindsExtremes();
  testNarrowRangeWidensAroundTheMiddle();
  testMissingTemperatures();
  testRainScale();
  testLabelStep();
  testGridStep();
  printf("weather forecast chart: OK\n");
  return 0;
}
