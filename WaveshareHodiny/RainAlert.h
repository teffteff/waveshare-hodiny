#pragma once

#include <stddef.h>
#include <stdint.h>

// Srážková předpověď z vlastního serveru (infra/rain) a rozhodnutí, jestli
// kvůli ní přepnout na radar. Oddělené od stahování i od LVGL, aby to šlo
// testovat na počítači - stejně jako LightningFeed vedle LightningService.
//
// Předpověď nepočítají hodiny ani server: publikuje ji ČHMÚ jako extrapolaci
// radaru na +10 až +60 minut. Server ji jen přečte v okolí polohy hodin
// a pošle dál odrazivost v dBZ:
//
//   {"time":1789916270,"slot":1789916100,"covered":true,"step":10,
//    "now":16,"steps":[24,16,12,20,28,28]}
//
// "now" je odrazivost nad hodinami teď, "steps" po řadě za 10, 20 ... minut.
// Nula znamená žádný odraz, -1 chybějící snímek - ty dva se nesmí zaměnit,
// protože z chybějícího snímku se nedá udělat závěr "neprší".

// ČHMÚ vydává šest kroků; strop je s rezervou, kdyby přidalo další.
constexpr size_t RAIN_FORECAST_MAX_STEPS = 12;

struct RainForecast {
  // Odrazivost v dBZ. 0 = žádný odraz, -1 = snímek chybí.
  int16_t now = -1;
  int16_t steps[RAIN_FORECAST_MAX_STEPS] = {};
  size_t stepCount = 0;
  // Délka jednoho kroku v minutách.
  uint8_t stepMinutes = 10;
  // Poloha leží v dosahu radarů. Bez toho prázdná předpověď neznamená sucho,
  // jen slepé místo, a hodiny z ní nesmí dělat závěr.
  bool covered = false;
  // Čas analýzy, ze které předpověď vyšla, a čas serveru. Podle nich se pozná
  // odpověď, která uvízla v mezipaměti.
  uint32_t slot = 0;
  uint32_t serverTime = 0;
};

// Rozebere odpověď serveru. Vrací false, když v ní chybí to podstatné.
bool rainForecastParse(const char *begin, const char *end,
                       RainForecast &forecast);

// Adresa dotazu na vlastní server z infra/rain: základní adresa z nastavení
// (klidně i se jménem a heslem pro basic_auth) doplněná o polohu a poloměr.
// Vrací false, když se do výstupu nevejde.
bool rainFeedBuildUrl(const char *baseUrl, float latitude, float longitude,
                      uint8_t radiusKm, char *output, size_t capacity);

struct RainAlertDecision {
  // Přepnout na radar.
  bool raise = false;
  // Za jak dlouho déšť dorazí, v minutách.
  uint16_t minutesAway = 0;
  // Odrazivost, která rozhodla.
  int16_t dbz = 0;
  // Nad hodinami prší už teď.
  bool rainingNow = false;
};

// Rozhodne, jestli se blíží déšť.
//
// Upozornění je na PŘÍCHOZÍ déšť, takže když prší už teď, nic nevyhlásí: kdo
// se dívá z okna, radar nepotřebuje. Vyhlásí se až na první krok do horizontu,
// který dosáhne prahu. Chybějící snímek (-1) se přeskočí, ne že by se počítal
// za sucho, a mimo dosah radaru (covered = false) se nevyhlašuje vůbec.
bool rainAlertEvaluate(const RainForecast &forecast, uint8_t minimumDbz,
                       uint8_t horizonMinutes, RainAlertDecision &decision);
