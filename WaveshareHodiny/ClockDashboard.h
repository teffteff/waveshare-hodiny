#pragma once

#include <Arduino.h>

#include "ClockConfig.h"
#include "PlaneRadarService.h"
#include "WeatherForecast.h"

struct ClockValues {
  int weatherCode = -1;
  bool weatherIsDay = true;
  bool sunStateAvailable = false;
  uint64_t nextSunriseTimestamp = 0;
  uint64_t nextSunsetTimestamp = 0;
  bool dayNightLightStateAvailable = false;
  bool dayNightLightOn = false;
  float leftTemperatureC = NAN;
  float rightTemperatureC = NAN;
  // Venkovní teplota pro stavový řádek radaru. Je vedená zvlášť, protože
  // pozice na ciferníku si majitel může přenastavit na cokoliv - třeba na
  // vlhkost - a radar potřebuje mít jistotu, že ukazuje teplotu venku.
  float outsideTemperatureC = NAN;
  float metricAValue = NAN;
  float metricBValue = NAN;
  // Hodnoty pro obrazovku CLOCK_STYLE_VALUES. Sloty 0-3 zrcadlí čtyři pole
  // výše, aby obě obrazovky ukazovaly totéž; sloty 4-8 mají vlastní entity.
  float slotValues[CLOCK_VALUE_SLOT_COUNT] = {NAN, NAN, NAN, NAN, NAN,
                                              NAN, NAN, NAN, NAN,
                                              NAN, NAN, NAN, NAN, NAN,
                                              NAN, NAN, NAN, NAN};
  bool homeAssistantOnline = false;
};

using BrightnessPreviewCallback = void (*)(uint8_t brightness);
using SettingsOpenCallback = void (*)();
using SettingsSaveCallback = void (*)(uint8_t clockStyle,
                                      uint8_t dayBrightness,
                                      uint8_t nightBrightness,
                                      bool automaticDayNight,
                                      bool secondRingEnabled,
                                      uint8_t secondEffect,
                                      bool animatedWeatherIcons,
                                      uint8_t weatherIconStyle,
                                      bool automaticFirmwareUpdate,
                                      uint8_t webMode);
using SettingsActionCallback = void (*)();
using RadarVisibilityCallback = void (*)(bool visible);
using RssVisibilityCallback = void (*)(bool visible);
using ForecastVisibilityCallback = void (*)(bool visible);
using RadarRangeCallback = void (*)(int8_t direction);

void clockDashboardInit(const ClockValues &values, uint8_t dayBrightness,
                        uint8_t nightBrightness, bool automaticDayNight,
                        BrightnessPreviewCallback brightnessPreview,
                        SettingsOpenCallback settingsOpen,
                        SettingsSaveCallback settingsSave,
                        SettingsActionCallback firmwareCheck,
                        SettingsActionCallback firmwareInstall,
                        RadarVisibilityCallback radarVisibility,
                        RadarRangeCallback radarRange,
                        RssVisibilityCallback rssVisibility,
                        ForecastVisibilityCallback forecastVisibility);
void clockDashboardLoop();
void clockDashboardShowSettings();
void clockDashboardShowSettingsPage(uint8_t page);
bool clockDashboardSettingsVisible();
// Zavře nastavení bez uložení; používá to podržení prstu, kterým se
// z nastavení odchází na sousední obrazovku.
void clockDashboardCloseSettings();
bool clockDashboardManualScreenChangeAllowed();
void clockDashboardSetNightMode(bool enabled);
bool clockDashboardNightModeEnabled();
uint8_t clockDashboardWeatherIconStyle(uint8_t configuredStyle);
// Dvojklepnutí přepíná denní a noční režim. Jedním klepnutím se přepínal, jenže
// se pletlo s podržením prstu, kterým se mění obrazovka.
void clockDashboardHandleDoubleTap();
bool clockDashboardRadarVisible();
void clockDashboardSetRadarVisible(bool visible);
bool clockDashboardRssVisible();
void clockDashboardSetRssVisible(bool visible);
// Kanál bez adresy nebo vypnutý se do rotace ani pod gesto nepustí.
void clockDashboardSetRssAvailable(bool available);
// Stav obrazovky zpráv. Při count == 0 se místo seznamu ukáže hláška;
// prázdná hláška znamená "načítám".
void clockDashboardSetRssStatus(const char *message, uint8_t count);
void clockDashboardSetRssItem(size_t index, const char *title,
                              const char *time);
// --- Agenda z kalendáře -----------------------------------------------------
bool clockDashboardAgendaVisible();
void clockDashboardSetAgendaVisible(bool visible);
// Vypnutá obrazovka nebo prázdná adresa se do rotace ani pod gesto nepustí.
// Stránka se zakládá až při prvním zapnutí, takže vypnutá agenda nestojí ani
// jeden objekt LVGL.
void clockDashboardSetAgendaAvailable(bool available);
void clockDashboardSetAgendaVisibilityCallback(RssVisibilityCallback visibility);
// Při count == 0 se místo seznamu ukáže hláška. Prázdná hláška znamená buď
// "načítám", nebo - s ready - že kalendář opravdu nic nemá; text si obrazovka
// skládá sama, aby se přepnutím jazyka přeložil i on. Volá se před sérií
// clockDashboardSetAgendaItem().
void clockDashboardSetAgendaStatus(const char *message, uint8_t count,
                                   bool ready);
// Neprázdný day znamená, že událost otevírá nový den a dostane nad sebe
// hlavičku. Prázdný time znamená celodenní událost.
void clockDashboardSetAgendaItem(size_t index, const char *day,
                                 const char *time, const char *title,
                                 uint8_t calendar);
// Jména kalendářů do legendy, v pořadí, ve kterém je posílá server - index se
// shoduje s parametrem calendar u události, takže legenda i časy dostanou
// stejnou barvu. Bez jmen se legenda nekreslí.
void clockDashboardSetAgendaCalendars(const char *const *names, size_t count);
bool clockDashboardForecastVisible();
void clockDashboardSetForecastVisible(bool visible);
// Vypnutá obrazovka se do rotace ani pod gesto nepustí.
void clockDashboardSetForecastAvailable(bool available);
// Kolik hodin se na obrazovku vejde vedle denní části a případné kvality
// ovzduší. Datová úloha podle toho pozná, kdy má smysl stahovat znovu, a web
// to ukazuje jako nápovědu u přepínače kvality ovzduší.
uint8_t clockDashboardForecastHourCapacity(const ClockForecastConfig &forecast);
// Předá staženou předpověď obrazovce.
void clockDashboardSetForecast(const WeatherForecastData &forecast);
// Dokud předpověď nedorazila, drží obrazovku hláška. Text si obrazovka skládá
// sama, aby se přepnutím jazyka přeložil i on.
void clockDashboardSetForecastFailed(bool failed);
// --- Radar letadel ----------------------------------------------------------
bool clockDashboardPlanesVisible();
void clockDashboardSetPlanesVisible(bool visible);
// Vypnutá obrazovka se do rotace ani pod gesto nepustí. Stránka se zakládá až
// při prvním zapnutí, takže vypnutý radar nestojí ani jeden objekt LVGL.
void clockDashboardSetPlanesAvailable(bool available);
void clockDashboardSetPlanesVisibilityCallback(RssVisibilityCallback visibility);
// Jedno klepnutí. Na mapě letadel vybere letadlo pod prstem nebo zavře jeho
// detail; na ostatních obrazovkách nedělá nic, protože denní režim přepíná
// dvojklepnutí.
void clockDashboardHandleSingleTap(int16_t x, int16_t y);
// Předá obrazovce hotový snímek radaru letadel i s detailem vybraného letu.
void clockDashboardSetPlanesSnapshot(const uint16_t *pixels, uint8_t shownCount,
                                     bool watchedVisible,
                                     const char *emergency, uint16_t rangeKm,
                                     const char *message, bool loading,
                                     bool haveAircraftData,
                                     const PlaneRadarDetail &detail);

bool clockDashboardAutomaticRotationAllowed();
// Switch the bank inside the values clock without changing the screen/dot.
bool clockDashboardSwipeValues();
void clockDashboardSetWifiAddress(const char *ipAddress);
void clockDashboardSetFirmwareVersion(const char *version,
                                      bool updateAvailable);
void clockDashboardSetFirmwareUpdateActive(bool active);
void clockDashboardSetFirmwareUpdateBlack(bool black);
void clockDashboardSetFirmwareUpdateCountdown(uint8_t seconds);
void clockDashboardSetWebActive(bool active);
void clockDashboardSetWifiConnected(bool connected);
void clockDashboardSetWebMode(uint8_t mode);
void clockDashboardApplyConfiguration(const ClockConfig &config);
void clockDashboardApplyAppearance(const ClockAppearanceConfig &appearance);
void clockDashboardUpdate(const ClockValues &values);
void clockDashboardSetDate(const char *dateText);
// Jmeniny pro dnešek VELKÝMI písmeny, nebo nullptr/"" když se nemají zobrazit.
void clockDashboardSetNameday(const char *nameday);
void clockDashboardSetSecond(uint8_t second);
void clockDashboardSetTime(const char *timeText);
void clockDashboardSetWeatherAnimation(const uint8_t *gifData, size_t size,
                                       const char *iconKey);
void clockDashboardSetRadarSnapshot(const uint16_t *pixels,
                                    const char *frameTime, uint16_t radiusKm,
                                    const char *message, bool loading,
                                    bool fullPreparationInProgress,
                                    bool latestFrame,
                                    uint8_t currentFrameNumber,
                                    uint8_t animationFrameCount,
                                    uint16_t displayedRadiusKm);
