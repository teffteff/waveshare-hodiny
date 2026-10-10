#pragma once

#include <lvgl.h>

// Písma Barlow pro rozhraní 7" displeje (tools/generate_lcd7_fonts.sh).
// Číselná písma mají jen číslice, čárku, tečku, dvojtečku a pomlčky.

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t lcd7_time176;
extern const lv_font_t lcd7_number72;
extern const lv_font_t lcd7_number44;
extern const lv_font_t lcd7_number38;
extern const lv_font_t lcd7_text28;
extern const lv_font_t lcd7_text22;
extern const lv_font_t lcd7_text18;
extern const lv_font_t lcd7_text15;

#ifdef __cplusplus
}
#endif
