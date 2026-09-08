#pragma once

#include <Arduino.h>

#include "ClockConfig.h"

inline const char *weatherIconStyleName(uint8_t style) {
  if (style == CLOCK_WEATHER_ICON_STYLE_FLAT) return "flat";
  if (style == CLOCK_WEATHER_ICON_STYLE_LINE) return "line";
  return "monochrome";
}

// Kód WMO z Open-Meteo na kód OpenWeather, kterým se v celém firmwaru vybírá
// ikona. Používá to jak aktuální počasí na ciferníku, tak každý řádek
// předpovědi, takže tabulka musí být jen jedna.
inline int weatherCodeFromWmo(int wmoCode) {
  if (wmoCode == 0) return 800;
  if (wmoCode == 1 || wmoCode == 2) return 801;
  if (wmoCode == 3) return 804;
  if (wmoCode == 45 || wmoCode == 48) return 741;
  if (wmoCode == 51 || wmoCode == 53 || wmoCode == 55) return 300;
  if (wmoCode == 56 || wmoCode == 57) return 511;
  if (wmoCode == 61 || wmoCode == 63 || wmoCode == 80 || wmoCode == 81)
    return 500;
  if (wmoCode == 65 || wmoCode == 82) return 502;
  if (wmoCode == 66 || wmoCode == 67) return 511;
  if (wmoCode == 71 || wmoCode == 73 || wmoCode == 77 || wmoCode == 85)
    return 600;
  if (wmoCode == 75 || wmoCode == 86) return 602;
  if (wmoCode == 95) return 200;
  if (wmoCode == 96 || wmoCode == 99) return 202;
  return -1;
}

enum class WeatherIconCondition : uint8_t {
  ClearDay,
  ClearNight,
  MostlyClearDay,
  MostlyClearNight,
  PartlyCloudyDay,
  PartlyCloudyNight,
  OvercastDay,
  OvercastNight,
  Overcast,
  Drizzle,
  Rain,
  Sleet,
  Snow,
  Mist,
  Thunderstorms,
  Unknown,
};

inline WeatherIconCondition weatherIconConditionForCode(int weatherCode,
                                                        bool isDay) {
  if (weatherCode >= 200 && weatherCode <= 232)
    return WeatherIconCondition::Thunderstorms;
  if (weatherCode >= 300 && weatherCode <= 321)
    return WeatherIconCondition::Drizzle;
  if (weatherCode >= 500 && weatherCode <= 510 && weatherCode != 511)
    return WeatherIconCondition::Rain;
  if (weatherCode == 511) return WeatherIconCondition::Sleet;
  if (weatherCode >= 520 && weatherCode <= 531)
    return WeatherIconCondition::Rain;
  if (weatherCode >= 600 && weatherCode <= 622)
    return WeatherIconCondition::Snow;
  if (weatherCode >= 701 && weatherCode <= 781)
    return WeatherIconCondition::Mist;
  if (weatherCode == 800)
    return isDay ? WeatherIconCondition::ClearDay
                 : WeatherIconCondition::ClearNight;
  if (weatherCode == 801)
    return isDay ? WeatherIconCondition::MostlyClearDay
                 : WeatherIconCondition::MostlyClearNight;
  if (weatherCode == 802)
    return isDay ? WeatherIconCondition::PartlyCloudyDay
                 : WeatherIconCondition::PartlyCloudyNight;
  if (weatherCode == 803)
    return isDay ? WeatherIconCondition::OvercastDay
                 : WeatherIconCondition::OvercastNight;
  if (weatherCode == 804) return WeatherIconCondition::Overcast;
  return WeatherIconCondition::Unknown;
}

inline const char *weatherIconConditionKey(WeatherIconCondition condition) {
  switch (condition) {
    case WeatherIconCondition::ClearDay:
      return "clear-day";
    case WeatherIconCondition::ClearNight:
      return "clear-night";
    case WeatherIconCondition::MostlyClearDay:
      return "mostly-clear-day";
    case WeatherIconCondition::MostlyClearNight:
      return "mostly-clear-night";
    case WeatherIconCondition::PartlyCloudyDay:
      return "partly-cloudy-day";
    case WeatherIconCondition::PartlyCloudyNight:
      return "partly-cloudy-night";
    case WeatherIconCondition::OvercastDay:
      return "overcast-day";
    case WeatherIconCondition::OvercastNight:
      return "overcast-night";
    case WeatherIconCondition::Overcast:
      return "overcast";
    case WeatherIconCondition::Drizzle:
      return "drizzle";
    case WeatherIconCondition::Rain:
      return "rain";
    case WeatherIconCondition::Sleet:
      return "sleet";
    case WeatherIconCondition::Snow:
      return "snow";
    case WeatherIconCondition::Mist:
      return "mist";
    case WeatherIconCondition::Thunderstorms:
      return "thunderstorms";
    case WeatherIconCondition::Unknown:
      return nullptr;
  }
  return nullptr;
}

inline const char *weatherAnimationKeyForCode(int weatherCode, bool isDay) {
  return weatherIconConditionKey(
      weatherIconConditionForCode(weatherCode, isDay));
}

inline bool weatherAnimationAssetKey(char *destination,
                                     size_t destinationSize,
                                     int weatherCode, bool isDay,
                                     uint8_t style) {
  const char *condition = weatherAnimationKeyForCode(weatherCode, isDay);
  if (destination == nullptr || destinationSize == 0 || condition == nullptr)
    return false;
  snprintf(destination, destinationSize, "%s-%s", weatherIconStyleName(style),
           condition);
  return true;
}
