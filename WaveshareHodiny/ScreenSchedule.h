#pragma once

#include <stdint.h>

#include "ClockConfig.h"

// Plán obrazovek: které pravidlo platí v danou chvíli. Oddělené od LVGL
// i od smyčky, aby šlo testovat na počítači.
//
// Výpočet nemá stav. Pro každé pravidlo se najde poslední začátek okna, který
// už nastal, a první konec po něm; okno platí, dokud konec nenastal. Tím se
// samo řeší okno přes půlnoc (od soumraku do svítání), posun východu Slunce
// den ode dne, restart hodin uprostřed okna i přechod na letní čas. Místní
// čas bere z localtime(), takže volající musí mít nastavenou časovou zónu.

// Okamžik události dne, jehož místní půlnoc je `localMidnight`, jako unixové
// sekundy. False, když událost ten den nenastane - za polárním kruhem Slunce
// některé dny nevyjde ani nezapadne.
bool screenScheduleEventOnDay(uint8_t event, int16_t value,
                              int64_t localMidnight, double latitudeDeg,
                              double longitudeDeg, int64_t &at);

// Index platného pravidla, nebo -1. Pravidla se čtou shora a první platné
// vyhrává. Vypnuté pravidlo (obrazovka CLOCK_SCREEN_ORDER_UNUSED) a pravidlo
// se stejným začátkem i koncem se přeskočí. `until` dostane konec okna.
int screenScheduleActiveRule(const ClockScreenScheduleConfig &schedule,
                             int64_t now, double latitudeDeg,
                             double longitudeDeg, int64_t *until = nullptr);
