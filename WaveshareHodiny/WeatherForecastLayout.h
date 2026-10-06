#pragma once

#include <stdint.h>

#include "WeatherForecast.h"

// Rozvržení obrazovky předpovědi, oddělené od kreslení, aby šlo testovat na
// počítači. První stránka je celá graf příštích hodin, druhá (tažením prstu)
// denní řádky a pod nimi kvalita ovzduší. Kruhový displej 480 x 480 ubírá
// šířku u okrajů: obsah řádku sahá od -150 do 152 px, takže se středy řádků
// musí vejít do pásu ±175.
constexpr int WEATHER_FORECAST_ROW_HEIGHT = 28;
constexpr int WEATHER_FORECAST_ROWS_TOP_Y = -168;
constexpr int WEATHER_FORECAST_ROWS_BOTTOM_Y = 172;
// Denní řádky jsou na své stránce sami, takže mají víc vzduchu než řádky
// kvality ovzduší.
constexpr int WEATHER_FORECAST_DAY_ROW_HEIGHT = 32;
constexpr int WEATHER_FORECAST_AIR_LINE_COUNT = 3;
constexpr int WEATHER_FORECAST_AIR_LINE_HEIGHT = 28;

// Kvalita ovzduší sedí u spodního okraje; poslední její řádek leží tam, kde
// končí pás řádků.
constexpr int WEATHER_FORECAST_AIR_TOP_Y =
    WEATHER_FORECAST_ROWS_BOTTOM_Y -
    (WEATHER_FORECAST_AIR_LINE_COUNT - 1) * WEATHER_FORECAST_AIR_LINE_HEIGHT;

// Svislá poloha středu denního řádku.
constexpr int weatherForecastDayRowY(uint8_t index) {
  return WEATHER_FORECAST_ROWS_TOP_Y +
         static_cast<int>(index) * WEATHER_FORECAST_DAY_ROW_HEIGHT;
}

// Graf zabírá celý pás řádků: od horní hrany prvního po dolní hranu
// posledního.
constexpr int WEATHER_FORECAST_CHART_TOP_Y =
    WEATHER_FORECAST_ROWS_TOP_Y - WEATHER_FORECAST_ROW_HEIGHT / 2;
constexpr int WEATHER_FORECAST_CHART_BOTTOM_Y =
    WEATHER_FORECAST_ROWS_BOTTOM_Y + WEATHER_FORECAST_ROW_HEIGHT / 2;
