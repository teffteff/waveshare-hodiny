#pragma once

#include <stddef.h>
#include <stdint.h>

#include "WeatherForecast.h"

// Naměřené počasí za posledních 24 hodin ze serveru (stanice, /api/clock,
// na hodinách adresa "history.json"). Kreslí ho obrazovka předpovědi: graf
// minulých 24 hodin a uplynulé hodiny vlevo od značky "teď" v grafu
// předpovědi, kde by jinak byl jen odhad modelu Open-Meteo. Rozbor je
// oddělený od stahování, aby šel testovat na počítači.

// Hodina, ve které server odpověděl, a 24 před ní.
constexpr size_t WEATHER_HISTORY_HOURS = 25;
// Teplota po deseti minutách za 25 hodin je 150 bodů; rezerva na zaokrouhlení.
constexpr size_t WEATHER_HISTORY_MAX_LINE = 160;

struct WeatherHistoryData {
  // Začátek první hodiny a čas odpovědi, sekundy od epochy (UTC).
  int64_t start = 0;
  int64_t now = 0;
  // Teplota v pravidelném kroku od lineStart; chybějící měření je NAN.
  int64_t lineStart = 0;
  uint32_t lineStep = 600;
  size_t lineCount = 0;
  float line[WEATHER_HISTORY_MAX_LINE];
  // Po hodinách: teplota na začátku hodiny (z čáry), vítr v km/h (průměr),
  // srážky v mm (úhrn), kód WMO (-1 = server ho neurčil, typicky v noci)
  // a jestli bylo slunce nad obzorem.
  WeatherForecastHour hours[WEATHER_HISTORY_HOURS];
  size_t hourCount = 0;
};

// Rozebere odpověď /history.json. Vrací false, když nenese ani hodiny, ani
// čáru teploty.
bool weatherHistoryParse(const char *payload, size_t length,
                         WeatherHistoryData &data);

// Adresa, na kterou se hodiny ptají: uložená adresa, a když sama teplotu
// nevybírá (parametr temp), doplní se temp_entity s entitou stavového řádku.
// Graf tak ukazuje totéž čidlo jako řádek nahoře, i když se změní. Vrací
// false, když se výsledek nevejde do `capacity`.
bool weatherHistoryBuildUrl(const char *base, const char *temperatureEntity,
                            char *url, size_t capacity);

// Teplota v čase `time` lineárně mezi dvěma body čáry, nebo NAN, když tam
// měření chybí.
float weatherHistoryLineAt(const WeatherHistoryData &data, int64_t time);
