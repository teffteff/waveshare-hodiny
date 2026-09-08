#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Rozbor předpovědi z Open-Meteo, oddělený od stahování, aby šel testovat na
// počítači - stejně jako RssParser vedle RssService nebo RainViewerIndex vedle
// RainViewerSource.
//
// Odpověď /v1/forecast má pevný tvar: nejdřív hlavička s polohou a časovým
// pásmem, pak "hourly_units", "hourly", "daily_units" a "daily". Uvnitř
// "hourly" i "daily" leží pole stejné délky, indexovaná společným polem
// "time". Klíče se v obou objektech opakují, takže se každý hledá až uvnitř
// své sekce.

// Kolik hodin se vejde na obrazovku i v nejvyšší variantě - devět řádků při
// skryté kvalitě ovzduší a bez denní části zbývá ještě rezerva.
constexpr size_t WEATHER_FORECAST_MAX_HOURS = 12;
// Open-Meteo posílá první den jako dnešek; obrazovka z něj ukazuje nanejvýš
// čtyři následující dny.
constexpr size_t WEATHER_FORECAST_MAX_DAYS = 4;

struct WeatherForecastHour {
  // Sekundy od epochy, ale posunuté do místního pásma - Open-Meteo je tak
  // s timezone=auto a timeformat=unixtime vydává.
  int64_t time = 0;
  float temperatureC = NAN;
  float precipitationMm = NAN;
  float windKmh = NAN;
  // Kód WMO, nebo -1 když ho odpověď pro tuto hodinu neměla.
  int weatherCode = -1;
  bool isDay = true;
};

struct WeatherForecastDay {
  int64_t time = 0;
  float maximumC = NAN;
  float minimumC = NAN;
  float precipitationMm = NAN;
  float windKmh = NAN;
  int weatherCode = -1;
};

// Kvalita ovzduší z air-quality-api.open-meteo.com. Pyl trav pokrývá jen
// evropská doména modelu CAMS, takže mimo Evropu zůstane NAN.
struct WeatherForecastAirQuality {
  float europeanAqi = NAN;
  float pm25 = NAN;
  float grassPollen = NAN;
};

struct WeatherForecastData {
  WeatherForecastHour hours[WEATHER_FORECAST_MAX_HOURS];
  size_t hourCount = 0;
  WeatherForecastDay days[WEATHER_FORECAST_MAX_DAYS];
  size_t dayCount = 0;
  WeatherForecastAirQuality air;
  bool airAvailable = false;
};

// Rozebere odpověď /v1/forecast. `fromTime` je nejstarší hodina, která se ještě
// smí ukázat - starší se přeskočí, takže odpověď začínající o půlnoci vydá
// hodiny od té současné dál. `fromDay` funguje stejně pro denní část, jen se
// porovnává s půlnocí daného dne; dnešek se tak dá vynechat.
// Vrací false, když v odpovědi nezůstala ani jedna hodina.
bool weatherForecastParse(const char *payload, int64_t fromTime,
                          int64_t fromDay, size_t wantedHours,
                          size_t wantedDays, WeatherForecastData &forecast);

// Rozebere odpověď /v1/air-quality. Chybějící veličina zůstane NAN; vrací
// false, když v odpovědi nebyla ani jedna z nich.
bool weatherForecastParseAirQuality(const char *payload,
                                    WeatherForecastAirQuality &air);
