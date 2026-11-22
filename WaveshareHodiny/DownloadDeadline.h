#pragma once

#include <stdint.h>

// Celkový čas na stažení jednoho těla odpovědi.
//
// Spojení, handshake i čtení mají vlastní limity, ty ale hlídají jen ticho:
// server, který posílá po bajtu těsně pod limitem nečinnosti, by úlohu držel
// ve stahování libovolně dlouho (a s ní TLS relaci v interní RAM a zámek
// NetworkOperationGuard). Proto má každé stažení ještě jeden strop, počítaný
// od začátku čtení těla. Po jeho vypršení čtení skončí jako nedokončené.
//
// Výchozí strop stačí na odpovědi do stovek kilobajtů i na pomalé Wi-Fi;
// větší soubory (svazek radaru, snímky ČHMÚ, firmware) mají vlastní.
constexpr uint32_t DOWNLOAD_DEFAULT_BUDGET_MS = 20000;
// Odpověď letadel má strop 512 kB.
constexpr uint32_t DOWNLOAD_LARGE_BUDGET_MS = 30000;
// Svazek radaru (až 160 kB) a snímky ČHMÚ čtené po 512 B s pauzou.
constexpr uint32_t DOWNLOAD_RADAR_BUDGET_MS = 45000;
// Obraz firmwaru má jednotky megabajtů a zapisuje se rovnou do flash.
constexpr uint32_t DOWNLOAD_FIRMWARE_BUDGET_MS = 10UL * 60UL * 1000UL;

// Čistá logika bez Arduina, aby šla testovat na počítači. Odečítání
// v uint32_t je bezpečné i přes přetečení millis() po 49,7 dne.
class DownloadDeadline {
 public:
  DownloadDeadline(uint32_t startedAtMs, uint32_t budgetMs)
      : startedAtMs_(startedAtMs), budgetMs_(budgetMs) {}

  bool expired(uint32_t nowMs) const {
    return static_cast<uint32_t>(nowMs - startedAtMs_) >= budgetMs_;
  }

 private:
  uint32_t startedAtMs_;
  uint32_t budgetMs_;
};
