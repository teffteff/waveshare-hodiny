#pragma once

// Retro LCD ciferník: sedmi- a čtrnáctisegmentové znaky s nerozsvícenými
// "duchy" segmentů pod nimi, dvě hodnoty a segmentový ukazatel. Port nápadu
// a kreslení z CooLajz/waveshare-hodiny (RetroLcd.cpp, licence MIT, viz
// THIRD_PARTY_NOTICES.md), napojený na zdejší sloty hodnot a rozvržený zvlášť
// pro kulatý 2,1" a pro 7" na šířku (RetroLcdLayout.h).
//
// Modul drží jediný objekt. Kreslí se sám v LV_EVENT_DRAW_MAIN a překresluje
// jen to, co se změnilo, takže snese částečný (direct) render.

#include <lvgl.h>
#include <stdint.h>
#include <time.h>

struct RetroLcdValue {
  const char *name;
  const char *unit;
  float value;
  uint8_t decimals;
  uint8_t digits;
  // Místo čísla ikona počasí (rastr z bodů jako na LCD).
  bool weather;
};

// Vytvoří ciferník velikosti rozvržení desky v rodiči na dané pozici. Vrací
// existující objekt, když už je vytvořený.
lv_obj_t *retroLcdCreate(lv_obj_t *parent, lv_coord_t x, lv_coord_t y);
lv_obj_t *retroLcdObject();

void retroLcdSetColors(uint32_t background, uint32_t foreground,
                       uint8_t ghostPercent, bool redNight);
void retroLcdSetOptions(uint8_t dateFormat, bool fixedWeekday, bool seconds,
                        bool leadingHourZero, bool english);
// valid = false kreslí pomlčky (čas ještě není ze sítě).
void retroLcdSetTime(const tm &local, bool valid);
void retroLcdSetValue(uint8_t index, const RetroLcdValue &value);
void retroLcdSetWeatherIcon(const lv_img_dsc_t *icon);
// label == nullptr ukazatel schová.
void retroLcdSetBar(const char *label, float value, float minimum,
                    float maximum, uint8_t segments);
void retroLcdSetStatus(bool wifi, bool web, bool homeAssistant, bool showHa,
                       bool lightning);
// Tečky obrazovek kreslí ciferník jen tam, kde má rozvržení místo (7").
void retroLcdSetDots(uint8_t count, uint8_t active);
