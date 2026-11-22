#pragma once

#include <stdint.h>

// Zhasnutý displej v noci (schéma 58, ClockDisplayExtrasConfig). Nápad
// z MeteoPlaneRadar (NightMode.cpp), stav a pravidla jsou vlastní.
//
// V nočním režimu (automatickém podle slunce i ručním) podsvícení úplně
// zhasne. Dotyk ho rozsvítí na nastavenou dobu a nic jiného neudělá: první
// dotyk na tmavý displej se nesmí propsat jako klepnutí ani tah, protože
// člověk neviděl, kam sahá. Dotyk na rozsvícený displej dobu prodlouží
// a projde normálně. Upozornění, které přepne obrazovku, displej rozsvítí na
// dobu svého držení (wakeFor). Den displej rozsvítí vždycky.
//
// Bez Arduina a LVGL, aby šla pravidla otestovat na počítači
// (tools/test_night_display.cpp). Čas je millis(); přetečení po 49 dnech
// zvládá porovnání rozdílem.
class NightDisplay {
 public:
  // Dotyk právě začal. Vrací true, když byl displej tmavý: tenhle dotyk jen
  // rozsvítí a celé gesto až do zvednutí prstu se zahodí.
  bool touch(uint32_t now, uint32_t wakeMs);

  // Rozsvítit nejméně na ms od now (upozornění, notifikace). Kratší čekání
  // nezkracuje delší, které už běží.
  void wakeFor(uint32_t now, uint32_t ms);

  // Vyhodnotí stav; vrací true, když má být podsvícení zhasnuté. enabled je
  // volba z nastavení, night noční režim a keepAwake něco, co displej právě
  // potřebuje (aktualizace firmwaru, notifikace bez konce).
  bool update(uint32_t now, bool enabled, bool night, bool keepAwake);

  // Výsledek posledního update() nebo touch().
  bool dark() const { return dark_; }
  // Za kolik ms rozsvícení skončí; 0, když neběží.
  uint32_t awakeRemainingMs(uint32_t now) const;

 private:
  bool dark_ = false;
  bool awake_ = false;
  uint32_t awakeUntil_ = 0;
};
