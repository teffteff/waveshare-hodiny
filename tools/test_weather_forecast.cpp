#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "WeatherForecast.h"

namespace {

// Zkrácená, jinak doslovná odpověď z api.open-meteo.com. Pořadí klíčů sedí na
// to, co server posílá: nejdřív "hourly_units" se stejnými názvy veličin jako
// "hourly", pak "daily_units" a "daily". Rozbor je tak nucený hledat klíče až
// uvnitř správné sekce.
const char *const REAL_FORECAST =
    "{\"latitude\":49.2,\"longitude\":16.6,\"generationtime_ms\":0.12,"
    "\"utc_offset_seconds\":7200,\"timezone\":\"Europe/Prague\","
    "\"timezone_abbreviation\":\"GMT+2\",\"elevation\":229.0,"
    "\"hourly_units\":{\"time\":\"unixtime\",\"temperature_2m\":\"°C\","
    "\"precipitation\":\"mm\",\"weather_code\":\"wmo code\","
    "\"wind_speed_10m\":\"km/h\",\"is_day\":\"\"},"
    "\"hourly\":{\"time\":[1757282400,1757286000,1757289600,1757293200],"
    "\"temperature_2m\":[16.8,17.1,null,18.4],"
    "\"precipitation\":[0.0,0.2,0.0,0.2],"
    "\"weather_code\":[61,61,3,1],"
    "\"wind_speed_10m\":[8.3,8.1,8.0,7.6],"
    "\"is_day\":[0,0,0,1]},"
    "\"daily_units\":{\"time\":\"unixtime\",\"weather_code\":\"wmo code\","
    "\"temperature_2m_max\":\"°C\",\"temperature_2m_min\":\"°C\","
    "\"precipitation_sum\":\"mm\",\"wind_speed_10m_max\":\"km/h\"},"
    "\"daily\":{\"time\":[1757196000,1757282400,1757368800,1757455200],"
    "\"weather_code\":[3,1,95,61],"
    "\"temperature_2m_max\":[25.9,27.4,28.2,24.1],"
    "\"temperature_2m_min\":[15.2,17.0,18.3,16.4],"
    "\"precipitation_sum\":[0.0,0.5,9.9,0.2],"
    "\"wind_speed_10m_max\":[14.0,16.2,13.4,11.5]}}";

const char *const REAL_AIR_QUALITY =
    "{\"latitude\":49.2,\"longitude\":16.6,\"generationtime_ms\":0.03,"
    "\"utc_offset_seconds\":7200,\"timezone\":\"Europe/Prague\","
    "\"current_units\":{\"time\":\"unixtime\",\"interval\":\"seconds\","
    "\"european_aqi\":\"EAQI\",\"pm2_5\":\"μg/m³\","
    "\"grass_pollen\":\"grains/m³\"},"
    "\"current\":{\"time\":1757282400,\"interval\":900,"
    "\"european_aqi\":26,\"pm2_5\":6.4,\"grass_pollen\":0.0}}";

bool nearly(float value, float expected) {
  return std::isfinite(value) && std::fabs(value - expected) < 0.001f;
}

void testRealResponse() {
  WeatherForecastData forecast;
  assert(weatherForecastParse(REAL_FORECAST, 0, 0, 12, 4, forecast));
  assert(forecast.hourCount == 4);
  assert(forecast.hours[0].time == 1757282400);
  assert(nearly(forecast.hours[0].temperatureC, 16.8f));
  assert(nearly(forecast.hours[0].precipitationMm, 0.0f));
  assert(nearly(forecast.hours[0].windKmh, 8.3f));
  assert(forecast.hours[0].weatherCode == 61);
  assert(!forecast.hours[0].isDay);
  assert(forecast.hours[3].isDay);
  // Klíče se opakují i v denní sekci; hodinová část si nesmí vzít její čísla.
  assert(forecast.dayCount == 4);
  assert(forecast.days[0].time == 1757196000);
  assert(forecast.days[0].weatherCode == 3);
  assert(nearly(forecast.days[1].maximumC, 27.4f));
  assert(nearly(forecast.days[1].minimumC, 17.0f));
  assert(nearly(forecast.days[2].precipitationMm, 9.9f));
  assert(nearly(forecast.days[3].windKmh, 11.5f));
}

// Chybějící veličina se posílá jako null. Pole se tím neposune, takže další
// hodina musí mít pořád svoji vlastní teplotu.
void testNullKeepsAlignment() {
  WeatherForecastData forecast;
  assert(weatherForecastParse(REAL_FORECAST, 0, 0, 12, 4, forecast));
  assert(!std::isfinite(forecast.hours[2].temperatureC));
  assert(forecast.hours[2].weatherCode == 3);
  assert(nearly(forecast.hours[3].temperatureC, 18.4f));
}

// Odpověď začíná půlnocí, obrazovka chce hodiny od té současné dál.
void testSkipsPastHours() {
  WeatherForecastData forecast;
  assert(weatherForecastParse(REAL_FORECAST, 1757289600, 1757282400, 12, 4,
                              forecast));
  assert(forecast.hourCount == 2);
  assert(forecast.hours[0].time == 1757289600);
  // Dnešek se v denní části vynechává, protože ho popisují hodiny nad ním.
  assert(forecast.dayCount == 3);
  assert(forecast.days[0].time == 1757282400);
}

void testRespectsRequestedCounts() {
  WeatherForecastData forecast;
  assert(weatherForecastParse(REAL_FORECAST, 0, 0, 2, 1, forecast));
  assert(forecast.hourCount == 2);
  assert(forecast.dayCount == 1);
  // Strop pole platí i tehdy, když si obrazovka řekne o víc.
  assert(weatherForecastParse(REAL_FORECAST, 0, 0, 99, 99, forecast));
  assert(forecast.hourCount == 4);
  assert(forecast.dayCount == 4);
}

void testRejectsUnusableResponses() {
  WeatherForecastData forecast;
  assert(!weatherForecastParse(nullptr, 0, 0, 12, 4, forecast));
  assert(!weatherForecastParse("{\"error\":true,\"reason\":\"No data\"}", 0, 0,
                               12, 4, forecast));
  // Useknutá odpověď: sekce začne, ale nikdy neskončí.
  assert(!weatherForecastParse(
      "{\"hourly\":{\"time\":[1757282400,1757286000", 0, 0, 12, 4, forecast));
  // Denní část bez hodinové nestačí - obrazovka stojí na hodinách.
  assert(!weatherForecastParse(
      "{\"daily\":{\"time\":[1757282400],\"weather_code\":[3]}}", 0, 0, 12, 4,
      forecast));
}

void testAirQuality() {
  WeatherForecastAirQuality air;
  assert(weatherForecastParseAirQuality(REAL_AIR_QUALITY, air));
  assert(nearly(air.europeanAqi, 26.0f));
  assert(nearly(air.pm25, 6.4f));
  assert(nearly(air.grassPollen, 0.0f));
}

// Mimo Evropu model CAMS pyl nepokrývá a hodnota přijde jako null. Zbytek
// odpovědi je použitelný, takže se sekce ukáže bez řádku s pylem.
void testAirQualityWithoutPollen() {
  const char *const payload =
      "{\"current_units\":{\"european_aqi\":\"EAQI\"},"
      "\"current\":{\"time\":1757282400,\"european_aqi\":31,\"pm2_5\":9.1,"
      "\"grass_pollen\":null}}";
  WeatherForecastAirQuality air;
  assert(weatherForecastParseAirQuality(payload, air));
  assert(nearly(air.europeanAqi, 31.0f));
  assert(!std::isfinite(air.grassPollen));
}

void testAirQualityRejectsEmptyResponse() {
  WeatherForecastAirQuality air;
  assert(!weatherForecastParseAirQuality(nullptr, air));
  assert(!weatherForecastParseAirQuality("{\"error\":true}", air));
  assert(!weatherForecastParseAirQuality(
      "{\"current\":{\"time\":1757282400,\"interval\":900}}", air));
}

}  // namespace

int main() {
  testRealResponse();
  testNullKeepsAlignment();
  testSkipsPastHours();
  testRespectsRequestedCounts();
  testRejectsUnusableResponses();
  testAirQuality();
  testAirQualityWithoutPollen();
  testAirQualityRejectsEmptyResponse();
  printf("weather forecast: OK\n");
  return 0;
}
