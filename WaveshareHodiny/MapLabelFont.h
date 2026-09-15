#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Popisky na mapových obrazovkách (letadla, družice) píše Montserrat 12 místo
// drobného pixelového písma 5x7: verzálky mají 9 px místo 7 a hladké hrany,
// takže se dají přečíst i z dálky.
//
// Obrazovky kreslí ve vlastních úlohách, zatímco LVGL tentýž font používá na
// hlavní obrazovce. Font si při hledání znaku zapisuje poslední nalezený do
// cache, takže každá úloha potřebuje vlastní kopii fontu s vlastní cache -
// sdílená by mohla občas podstrčit cizí znak. Bitmapy jsou nekomprimované
// a jen se čtou. Instance proto patří jediné úloze a nesmí se sdílet.
class MapLabelFont {
 public:
  // Obálka popisku: verzálky a nad i pod nimi tři pixely, stejně jako u
  // pixelového písma.
  int labelHeight();
  int width(const char *text);
  // Text VELKÝMI písmeny s horní hranou verzálek na y.
  void draw(uint16_t *pixels, int x, int y, const char *text, uint16_t color,
            uint8_t opacity = 100);

 private:
  void ensureInitialized();

  lv_font_fmt_txt_glyph_cache_t cache_ = {};
  lv_font_fmt_txt_dsc_t dsc_ = {};
  lv_font_t font_ = {};
  int capHeight_ = 0;
  bool initialized_ = false;
};
