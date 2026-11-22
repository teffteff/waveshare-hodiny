#pragma once

// Rozvržení retro LCD ciferníku pro obě desky. Jen čísla bez LVGL, aby se
// dalo na počítači ověřit, že se všechno vejde do kruhu 2,1" i na plochu 7"
// a nic se nepřekrývá (tools/test_retro_lcd.cpp).
//
// Kulatý 480 x 480 drží rozvržení z CooLajz/waveshare-hodiny: den a datum
// nahoře, velký čas se vteřinami uprostřed, pod čarou dvě hodnoty a pod nimi
// segmentový ukazatel a stavové ikony. 7" 800 x 480 je navržené na šířku:
// den vlevo a datum vpravo v horním pruhu, přes celou výšku středu čas
// s vteřinami a ikonou počasí vpravo, dole tři sloupce - dvě hodnoty
// a ukazatel se stavem.

#include <cstdint>

struct RetroLcdCell {
  // Levý horní roh prvního znaku, rozměr jednoho znaku a mezera mezi znaky.
  int16_t x, y, w, h, gap;
};

struct RetroLcdBox {
  int16_t x, y, w, h;
};

struct RetroLcdLayout {
  int16_t width, height;
  bool round;
  // Den v týdnu (čtrnáctisegmentovka) a datum: x je střed řádku.
  RetroLcdCell weekday;
  RetroLcdCell date;
  // Čas HH:MM a vteřiny; x času platí se vteřinami, bez nich se čas
  // vystředí na timeCenterX (0 = zůstane na místě, na 7" vedle něj leží
  // ikona počasí).
  RetroLcdCell time;
  int16_t timeCenterX;
  RetroLcdCell seconds;
  // Ikona počasí jen na 7" (vpravo nad vteřinami); w = 0 znamená žádná.
  RetroLcdBox weather;
  // Vodorovné čáry (h = 1) a svislé dělítko mezi hodnotami (w = 1).
  RetroLcdBox rules[2];
  RetroLcdBox dividers[2];
  // Dvě hodnoty: střed, název nad číslem, číslo a jednotka.
  int16_t valueCenterX[2];
  int16_t valueWidth;
  int16_t valueNameY;
  // Spodní hrana číslic; výška klesá, když se číslo s jednotkou nevejde.
  int16_t valueBottom;
  int16_t valueDigitW, valueDigitH, valueGap;
  int16_t valueUnitY;
  // Oblast, kterou zabírá ikona počasí místo hodnoty.
  int16_t valueWeatherTop, valueWeatherHeight;
  // Segmentový ukazatel s popiskem pod ním (kulatý) nebo nad ním (7").
  RetroLcdBox bar;
  RetroLcdBox barLabel;
  // Stavové ikony: střed řady a horní hrana.
  int16_t statusCenterX, statusY;
  // Tečky obrazovek kreslí retro LCD jen na 7" (kulatý má vlastní překryv);
  // x je pravý okraj řady.
  int16_t dotsRight, dotsY;
};

constexpr RetroLcdLayout RETRO_LCD_LAYOUT_ROUND = {
    480, 480, true,
    {240, 57, 18, 30, 6},
    {240, 106, 16, 29, 5},
    {50, 154, 60, 112, 12},
    240,
    {372, 216, 27, 50, 7},
    {0, 0, 0, 0},
    {{38, 278, 404, 1}, {0, 0, 0, 0}},
    {{240, 291, 1, 69}, {0, 0, 0, 0}},
    {141, 339},
    182,
    289,
    361,
    24, 46, 5,
    338,
    280, 103,
    {108, 386, 265, 22},
    {108, 416, 265, 18},
    240, 445,
    0, 0,
};

constexpr RetroLcdLayout RETRO_LCD_LAYOUT_LCD7 = {
    800, 480, false,
    {200, 38, 26, 44, 8},
    {580, 38, 24, 44, 7},
    {44, 120, 104, 200, 20},
    0,
    {604, 230, 48, 90, 12},
    {596, 116, 120, 104},
    {{36, 98, 728, 1}, {36, 340, 728, 1}},
    {{272, 354, 1, 100}, {528, 354, 1, 100}},
    {154, 400},
    228,
    352,
    442,
    32, 60, 6,
    414,
    344, 124,
    {548, 392, 212, 30},
    {548, 354, 212, 22},
    654, 446,
    // Tečky na stejném místě jako na ostatních domovských obrazovkách 7"
    // (pageDotsLeft), aby na ně šlo klepnout.
    780, 20,
};

#if HODINY_BOARD_LCD7
constexpr const RetroLcdLayout &RETRO_LCD_LAYOUT = RETRO_LCD_LAYOUT_LCD7;
#else
constexpr const RetroLcdLayout &RETRO_LCD_LAYOUT = RETRO_LCD_LAYOUT_ROUND;
#endif

// Šířka řetězce znaků v buňce: tečka a dvojtečka jsou úzké, oddělovač data
// ('-' nebo '/' na místě oddělovače) poloviční. Stejné pravidlo kreslí
// RetroLcd.cpp, takže z něj jde spočítat, kam se text vejde.
inline int retroLcdTextWidth(const char *text, int w, int gap,
                             bool dateSymbols = false,
                             int separatorA = 2, int separatorB = 5) {
  int total = 0;
  for (const char *c = text; *c; ++c) {
    const int position = static_cast<int>(c - text);
    int cw = w;
    if (*c == '.' || *c == ':') {
      cw = w / 3 > 4 ? w / 3 : 4;
    } else if (dateSymbols && (position == separatorA || position == separatorB) &&
               (*c == '-' || *c == '/')) {
      cw = w / 2 > 5 ? w / 2 : 5;
    }
    total += cw + gap;
  }
  return total > gap ? total - gap : 0;
}
