#pragma once

#include <stdint.h>

#include "WeatherForecast.h"

// Rozpočet svislého místa na obrazovce předpovědi, oddělený od kreslení, aby
// šel testovat na počítači. Kruhový displej 480 x 480 ubírá šířku u okrajů:
// obsah řádku sahá od -150 do 152 px, takže se středy řádků musí vejít do pásu
// ±175. Z toho plyne, kolik hodin po denní části a kvalitě ovzduší zbude - a
// právě proto se počet hodin nenastavuje, ale dopočítává.
constexpr int WEATHER_FORECAST_ROW_HEIGHT = 28;
constexpr int WEATHER_FORECAST_ROWS_TOP_Y = -168;
constexpr int WEATHER_FORECAST_ROWS_BOTTOM_Y = 172;
// Mezera mezi hodinovou a denní částí, ve které leží dělicí čára.
constexpr int WEATHER_FORECAST_DIVIDER_GAP = 12;
constexpr int WEATHER_FORECAST_AIR_LINE_COUNT = 3;
constexpr int WEATHER_FORECAST_AIR_LINE_HEIGHT = 28;

// Spodní hranice středu posledního řádku. Kvalita ovzduší si bere tři řádky
// pod předpovědí, takže o ně pás zkracuje.
constexpr int weatherForecastRowsBottomY(bool airQuality) {
  return airQuality ? WEATHER_FORECAST_ROWS_BOTTOM_Y -
                          WEATHER_FORECAST_AIR_LINE_COUNT *
                              WEATHER_FORECAST_AIR_LINE_HEIGHT
                    : WEATHER_FORECAST_ROWS_BOTTOM_Y;
}

// Střed prvního řádku kvality ovzduší. Leží o půl výšky obou řádků pod
// hranicí předpovědi, takže se sekce s posledním řádkem nedotkne ani tehdy,
// když si hodiny vezmou celý pás.
constexpr int WEATHER_FORECAST_AIR_TOP_Y =
    weatherForecastRowsBottomY(true) +
    (WEATHER_FORECAST_ROW_HEIGHT + WEATHER_FORECAST_AIR_LINE_HEIGHT) / 2;

// Kolik hodin se vejde vedle `dayCount` dnů a případné kvality ovzduší.
// Nikdy nevrátí nulu: obrazovka bez jediné hodiny by neměla co ukázat.
constexpr uint8_t weatherForecastHourCapacity(bool airQuality,
                                              uint8_t dayCount) {
  const uint8_t days = dayCount > WEATHER_FORECAST_MAX_DAYS
                           ? static_cast<uint8_t>(WEATHER_FORECAST_MAX_DAYS)
                           : dayCount;
  int height = weatherForecastRowsBottomY(airQuality) -
               WEATHER_FORECAST_ROWS_TOP_Y + WEATHER_FORECAST_ROW_HEIGHT;
  if (days > 0)
    height -= WEATHER_FORECAST_DIVIDER_GAP + days * WEATHER_FORECAST_ROW_HEIGHT;
  const int hours = height / WEATHER_FORECAST_ROW_HEIGHT;
  if (hours < 1) return 1;
  if (hours > static_cast<int>(WEATHER_FORECAST_MAX_HOURS))
    return static_cast<uint8_t>(WEATHER_FORECAST_MAX_HOURS);
  return static_cast<uint8_t>(hours);
}

// Svislá poloha středu řádku. Hodiny jdou od horního okraje dolů, denní část
// za nimi až po mezeře s dělicí čárou.
constexpr int weatherForecastRowY(uint8_t index, uint8_t hourCount) {
  const int y = WEATHER_FORECAST_ROWS_TOP_Y +
                static_cast<int>(index) * WEATHER_FORECAST_ROW_HEIGHT;
  return index >= hourCount ? y + WEATHER_FORECAST_DIVIDER_GAP : y;
}
