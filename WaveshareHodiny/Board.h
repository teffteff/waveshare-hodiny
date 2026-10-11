#pragma once

#include <stdint.h>

// Deska se volí při překladu: bez přepínače vzniká firmware pro kulatý
// ESP32-S3-Touch-LCD-2.1, s -DHODINY_BOARD_LCD7=1 pro ESP32-S3-Touch-LCD-7.
// Obě desky mají stejný čip, flash i PSRAM, takže se liší jen ovladače
// displeje, dotyku, expandéru a rozměry obrazovky.
#ifndef HODINY_BOARD_LCD7
#define HODINY_BOARD_LCD7 0
#endif
#define HODINY_BOARD_LCD21 (!HODINY_BOARD_LCD7)

#if HODINY_BOARD_LCD7
#define BOARD_ID "lcd7"
#define BOARD_NAME "ESP32-S3-Touch-LCD-7"
constexpr int16_t SCREEN_WIDTH = 800;
constexpr int16_t SCREEN_HEIGHT = 480;
#else
#define BOARD_ID "lcd21"
#define BOARD_NAME "ESP32-S3-Touch-LCD-2.1"
constexpr int16_t SCREEN_WIDTH = 480;
constexpr int16_t SCREEN_HEIGHT = 480;
#endif

// Schopnosti desky a z nich plynoucí pravidla na jednom místě. Kód i webové
// nastavení se ptají tady, místo aby každý sám rozhodoval podle
// HODINY_BOARD_LCD7; web z toho pozná, která pole na téhle desce nic nedělají.
struct BoardProfile {
  const char *id;
  // Kulatý displej; obdélníku stránky kreslené pro kruh nic neořezává.
  bool roundScreen;
  // Podsvícení jde stmívat. 7" ho má jen zapnuté, nebo vypnuté (CH422G EXIO2).
  bool backlightDimmable;
  // Samostatná obrazovka Slunce a Měsíc. 7" ji nemá, údaje ukazuje sloupec
  // družic, a tak Slunce a Měsíc počítá vždycky.
  bool skyScreen;
  // Počet zpráv z nastavení. 7" posouvá seznam a bere všechno, co kanál dá.
  bool rssItemCountHonoured;
  // Kulatý ciferník se vším, co k němu patří: písmo a barvy digitálních
  // hodin, dvojtečka, vteřinový prstenec, hodnoty a datum uvnitř analogového
  // ciferníku. Domovská obrazovka 7" je kreslí po svém (ClockDashboardLcd7.h).
  bool roundClockFace;
  // Hodiny dopředu, které stahuje předpověď; 7" má navíc stránku se zítřkem.
  uint8_t forecastAheadHours;
  // Svislý poloměr pohledu na celou republiku. Kulatý má čtverec, takže musí
  // pokrýt šířku Česka (asi 490 km); obdélník 7" je o třetinu širší, a tak
  // mu stačí menší poloměr - Česko pak vyplní obraz a rohy nesahají za kraj
  // kompozice ČHMÚ (šedé "bez dat").
  uint16_t radarWholeCountryRadiusKm;
  // Rozsahy meteoradaru pro swipe i web; poslední (0) je celá ČR. 7" má
  // celou republiku na celý obraz, takže 200 km je zbytečný mezikrok.
  uint16_t radarRanges[5];
  uint8_t radarRangeCount;
};

#if HODINY_BOARD_LCD7
constexpr BoardProfile BOARD_PROFILE = {
    BOARD_ID, false, false, false, false, false, 48, 195, {25, 50, 100, 0}, 4,
};
#else
constexpr BoardProfile BOARD_PROFILE = {
    BOARD_ID, true, true, true, true, true, 24, 260, {25, 50, 100, 200, 0}, 5,
};
#endif

constexpr bool SCREEN_ROUND = BOARD_PROFILE.roundScreen;
constexpr bool BOARD_BACKLIGHT_DIMMABLE = BOARD_PROFILE.backlightDimmable;

// Jeviště: čtverec, do kterého se kreslí stávající stránky navržené pro
// kulatý displej 480 x 480. Na 2,1" je to celá obrazovka, na 7" pravá část
// vedle levého pruhu (800 = 320 + 480).
constexpr int16_t STAGE_SIZE = 480;
constexpr int16_t STAGE_X = SCREEN_WIDTH - STAGE_SIZE;
constexpr int16_t STAGE_Y = 0;

static_assert(SCREEN_HEIGHT == STAGE_SIZE,
              "jeviště musí mít výšku obrazovky");
