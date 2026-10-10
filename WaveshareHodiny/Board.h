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
// Displej je obdélník; stránky kreslené pro kruh v něm nic neořezává.
constexpr bool SCREEN_ROUND = false;
// Podsvícení je jen zapnuté, nebo vypnuté (CH422G EXIO2).
constexpr bool BOARD_BACKLIGHT_DIMMABLE = false;
#else
#define BOARD_ID "lcd21"
#define BOARD_NAME "ESP32-S3-Touch-LCD-2.1"
constexpr int16_t SCREEN_WIDTH = 480;
constexpr int16_t SCREEN_HEIGHT = 480;
constexpr bool SCREEN_ROUND = true;
constexpr bool BOARD_BACKLIGHT_DIMMABLE = true;
#endif

// Jeviště: čtverec, do kterého se kreslí stávající stránky navržené pro
// kulatý displej 480 x 480. Na 2,1" je to celá obrazovka, na 7" pravá část
// vedle levého pruhu (800 = 320 + 480).
constexpr int16_t STAGE_SIZE = 480;
constexpr int16_t STAGE_X = SCREEN_WIDTH - STAGE_SIZE;
constexpr int16_t STAGE_Y = 0;

static_assert(SCREEN_HEIGHT == STAGE_SIZE,
              "jeviště musí mít výšku obrazovky");
