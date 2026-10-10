#pragma once

#include <Arduino.h>
#include <lvgl.h>

void displayDriverInit();
void displayDriverLoop();
void displayDriverRefresh();
// Čtverec 480 x 480, do kterého se kreslí stránky navržené pro kulatý
// displej. Na 2,1" je to přímo aktivní obrazovka, na 7" její pravá část
// (STAGE_X, STAGE_Y z Board.h). Vytvoří se při prvním volání.
lv_obj_t *displayDriverStage();
void displayDriverSetPartialRefresh(bool enabled, bool rebuildBuffers = false);
// Gesta se rozpoznávají v software z hrubých souřadnic dotyku; gestový
// registr řadiče CST820 se nepoužívá. Vyhodnocení proběhne až po zvednutí
// prstu, takže se držení, přetažení a klepnutí nikdy nepletou.
// Podržení prstu: -1 předchozí obrazovka, +1 další, 0 nic.
int8_t displayDriverTakeScreenHold();
// Přetažení prstem: -1 přiblížit rozsah radaru, +1 oddálit, 0 nic.
int8_t displayDriverTakeRangeSwipe();
// Krátké klepnutí i s místem, kam dopadlo. Souřadnice potřebuje radar letadel,
// který jimi vybírá letadlo pod prstem. Klepnutí se hlásí až po okně
// dvojklepnutí, jinak by první polovina dvojklepnutí zároveň vybrala letadlo.
bool displayDriverTakeShortTap(int16_t &x, int16_t &y);
// Dvě klepnutí těsně po sobě a na stejném místě. Přepíná denní a noční režim;
// jedno klepnutí ho záměrně nepřepíná, protože se pletlo s podržením prstu.
bool displayDriverTakeDoubleTap();
bool displayDriverBeginFramebufferCapture(Print &output);
bool displayDriverStreamFramebufferChunk(Print &output);
