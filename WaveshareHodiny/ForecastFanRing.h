#pragma once

// Prstenec předpovědi kolem kulatého ciferníku (logika v ForecastFan.h).
// Pixely mezikruží leží v PSRAM jen tehdy, když je prstenec zapnutý; kreslí
// se kopírováním úseků řádků, takže plné překreslení digitálního ciferníku
// každou vteřinu nic nestojí. Změna předpovědi nebo posun švu přepočítá jen
// dotčené výseky; schovaný prstenec se dopočítává po kouscích ve smyčce.

#include <lvgl.h>

#include "ForecastFan.h"

// Mezikruží na kulatém displeji: vně vteřinových teček (poloměr 226) až
// k okraji, přes vnější kroužek analogového ciferníku.
constexpr int FORECAST_FAN_RING_INNER = 231;
constexpr int FORECAST_FAN_RING_OUTER = 240;

// Vytvoří průhledný objekt 480 x 480 se středem prstence uprostřed. Buffer se
// alokuje až při prvním klíči.
lv_obj_t *forecastFanRingCreate(lv_obj_t *parent);
lv_obj_t *forecastFanRingObject();
void forecastFanRingSetKey(const ForecastFanKey &key);
// Dopočítá rozpracované výseky. Viditelný prstenec se dopočítá celý najednou
// (aby se nikdy neukázala směs starých a nových barev), schovaný nejvýš po
// rowBudget řádcích. Vrací true, když je hotovo.
bool forecastFanRingMaintain(bool visible, int rowBudget = 12);
// Uvolní buffer (prstenec vypnutý).
void forecastFanRingRelease();
size_t forecastFanRingBytes();
