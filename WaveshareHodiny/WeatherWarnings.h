#pragma once

#include <stddef.h>
#include <stdint.h>

// Výstrahy ČHMÚ z vlastního serveru (infra/warnings) a rozhodnutí, co z nich
// ukázat a kdy kvůli nim přepnout na radar. Oddělené od stahování i od LVGL,
// aby to šlo testovat na počítači - stejně jako RainAlert.
//
// ČHMÚ vydává výstrahy jako CAP soubor o dvou megabajtech s kódy ORP. Server ho
// stáhne, polohu hodin převede na ORP a pošle jen výstrahy pro ni:
//
//   {"v":1,"time":1789320000,"sent":1789290955,"covered":true,"orp":"2105",
//    "area":"Černošice","stale":false,
//    "warnings":[{"id":"SIVS X.2","lvl":3,"type":3,"ev":"Velmi silné bouřky",
//                 "on":1789297200,"ex":1789336800}]}
//
// Seřazené od nejvážnější. "lvl" je stupeň z CAP: 2 žlutá, 3 oranžová,
// 4 červená. "ex" = 0 znamená "do odvolání".

constexpr size_t WEATHER_WARNING_MAX = 6;
constexpr size_t WEATHER_WARNING_ID_LENGTH = 25;
constexpr size_t WEATHER_WARNING_EVENT_LENGTH = 64;
constexpr size_t WEATHER_WARNING_AREA_LENGTH = 48;
// Výstraha, která začne později než za den, na radar ještě nepatří.
constexpr int64_t WEATHER_WARNING_SHOW_AHEAD_SECONDS = 24 * 3600;
// Na radar se přepne nejdřív hodinu před začátkem výstrahy; vydaná den
// dopředu by jinak přepnula uprostřed noci na něco, co přijde až zítra.
constexpr int64_t WEATHER_WARNING_SWITCH_AHEAD_SECONDS = 3600;

struct WeatherWarning {
  // Kód jevu i se stupněm, stejný při každé aktualizaci téže výstrahy.
  char id[WEATHER_WARNING_ID_LENGTH] = "";
  uint8_t level = 0;
  uint8_t type = 0;
  char event[WEATHER_WARNING_EVENT_LENGTH] = "";
  int64_t onset = 0;
  // 0 = do odvolání.
  int64_t expires = 0;
};

struct WeatherWarningFeed {
  // Poloha leží v některé ORP. Mimo Česko ČHMÚ výstrahy nevydává a prázdný
  // seznam tam neznamená klid.
  bool covered = false;
  // Server posílá starší stav, protože poslední stažení z ČHMÚ selhalo.
  bool stale = false;
  char area[WEATHER_WARNING_AREA_LENGTH] = "";
  uint32_t serverTime = 0;
  WeatherWarning items[WEATHER_WARNING_MAX];
  size_t count = 0;
};

// Rozebere odpověď serveru. Vrací false, když v ní chybí to podstatné.
bool weatherWarningsParse(const char *begin, const char *end,
                          WeatherWarningFeed &feed);

// Adresa dotazu: základní adresa z nastavení (klidně se jménem a heslem)
// doplněná o polohu a jazyk. Vrací false, když se do výstupu nevejde.
bool weatherWarningsBuildUrl(const char *baseUrl, float latitude,
                             float longitude, bool english, char *output,
                             size_t capacity);

// Platí výstraha teď, nebo začne do dne? Skončené a vzdálené ne.
bool weatherWarningRelevant(const WeatherWarning &warning, int64_t now);

// Výstraha na řádek radaru: nejvážnější relevantní od minimumLevel. `others`
// dostane počet dalších takových. Vrací nullptr, když žádná není.
const WeatherWarning *weatherWarningsTop(const WeatherWarningFeed &feed,
                                         uint8_t minimumLevel, int64_t now,
                                         uint8_t &others);

// Text řádku: "Velmi silné bouřky do 24:00", "Silný vítr od zítra 06:00",
// u dalších výstrah "+2" na konci. Časy v místním čase hodin.
void weatherWarningBannerText(const WeatherWarning &warning, uint8_t others,
                              int64_t now, bool english, char *output,
                              size_t capacity);
// Totéž bez názvu jevu: " do 24:00  +2". Obrazovka zkracuje jen název, aby
// čas zůstal vidět celý.
void weatherWarningBannerTail(const WeatherWarning &warning, uint8_t others,
                              int64_t now, bool english, char *output,
                              size_t capacity);

// Výstraha, kvůli které se má přepnout na radar: stupeň aspoň switchLevel,
// platí teď nebo do hodiny začne, a její id ještě není mezi `alerted`.
// Vrací nullptr, když taková není nebo je switchLevel nula.
const WeatherWarning *weatherWarningsToAnnounce(const WeatherWarningFeed &feed,
                                                uint8_t switchLevel,
                                                int64_t now,
                                                const char alerted[][WEATHER_WARNING_ID_LENGTH],
                                                size_t alertedCount);
