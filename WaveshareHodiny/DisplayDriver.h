#pragma once

#include <Arduino.h>
#include <lvgl.h>

void displayDriverInit();
void displayDriverLoop();
void displayDriverRefresh();
void displayDriverSetPartialRefresh(bool enabled, bool rebuildBuffers = false);
// Gesta se rozpoznávají v software z hrubých souřadnic dotyku; gestový
// registr řadiče CST820 se nepoužívá. Vyhodnocení proběhne až po zvednutí
// prstu, takže se držení, přetažení a klepnutí nikdy nepletou.
// Podržení prstu: -1 předchozí obrazovka, +1 další, 0 nic.
int8_t displayDriverTakeScreenHold();
// Přetažení prstem: -1 přiblížit rozsah radaru, +1 oddálit, 0 nic.
int8_t displayDriverTakeRangeSwipe();
bool displayDriverTakeShortTap();
bool displayDriverBeginFramebufferCapture(Print &output);
bool displayDriverStreamFramebufferChunk(Print &output);
