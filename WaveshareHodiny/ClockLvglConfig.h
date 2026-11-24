#pragma once

// Samostatná projektová konfigurace pro LVGL 8.3.x. Záměrně se neopírá o
// ručně vytvořený lv_conf.h uvnitř lokální instalace knihovny, aby čistý
// checkout používal stejné volby na každém počítači.
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_GIF 1
#define LV_USE_QRCODE 1

// Widgety, které firmware nepoužívá. Výchozí téma na ně odkazuje, takže by se
// jinak přilinkovaly i nepoužité (graf, budík, kalendář, klávesnice...).
// Na 7" leží kód v PSRAM po 64kB stránkách a každá stránka navíc ubere
// stejně PSRAM pro data; používají se jen obj, label, btn, img, canvas, line,
// arc, slider (s bar), switch, dropdown, gif a qrcode.
#define LV_USE_BTNMATRIX 0
#define LV_USE_CHECKBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

// Malé objekty zůstávají v rychlé interní SRAM, velké grafické alokace jsou
// směrovány do 8MB OPI PSRAM přes projektový allocator.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "ClockLvglMemory.h"
#define LV_MEM_CUSTOM_ALLOC clockLvglAlloc
#define LV_MEM_CUSTOM_REALLOC clockLvglRealloc
#define LV_MEM_CUSTOM_FREE clockLvglFree
