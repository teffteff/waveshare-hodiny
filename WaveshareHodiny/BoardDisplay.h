#pragma once

#include <stdint.h>

#include "Board.h"

// Rozhraní desky pro zbytek firmwaru. Implementuje ho ovladač vybrané desky
// (Display_ST7701.cpp pro 2,1", Display_LCD7.cpp pro 7"); společnou část RGB
// panelu RgbPanel.cpp.

// I2C a expandér; volat jako první, dřív než cokoli sáhne na displej.
void boardInit();

void LCD_Init();
void LCD_Resync();
// Pozná trvalý posun obrazu po vypadlém přerušení bounce bufferu a panel
// resynchronizuje. Volat průběžně z hlavní smyčky; vrací true při opravě.
bool LCD_MaintainSync();
uint32_t LCD_SyncRepairCount();
bool LCD_SetPixelClock(uint32_t frequencyHz);
uint32_t LCD_GetPixelClock();
void LCD_Sleep();
void LCD_Wake();
bool LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend,
                   uint16_t Yend, uint8_t *color);

// Jas 0-100. Deska bez stmívání (BOARD_BACKLIGHT_DIMMABLE == false) jen
// zapne podsvícení pro nenulovou hodnotu a vypne pro nulu.
void Set_Backlight(uint8_t Light);

// Jeden bod dotyku v souřadnicích celé obrazovky. Vrací false, když se nikdo
// nedotýká nebo čtení selhalo.
bool boardTouchRead(uint16_t &x, uint16_t &y);
