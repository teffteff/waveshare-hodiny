#pragma once

#include <Arduino.h>

#include "RainAlert.h"

// Srážková předpověď z vlastního serveru (infra/rain).
//
// Předpověď počítá ČHMÚ a server ji jen přečte v okolí polohy hodin, takže
// tahle služba nedělá nic než jeden dotaz za pět minut a dvě stě bajtů
// odpovědi. Rozhodnutí "přepnout na radar" tady nepadá: to dělá smyčka
// v hodinách podle nastavených prahů, stejně jako u blesků.
//
// Vlastní úloha je tu ze stejného důvodu jako u blesků a rozvrhu: adresu zadává
// majitel, takže se ověřuje proti svazku kořenů Mozilly, a na to je potřeba
// výrazně větší zásobník, než má úloha home-assistant.

struct RainAlertStatus {
  // Roste s každou odpovědí, i neúspěšnou, aby obrazovka poznala změnu.
  uint32_t generation = 0;
  bool enabled = false;
  bool ready = false;
  RainForecast forecast;
  // Stáří posledního úspěchu; platí jen s lastSuccessAvailable.
  uint32_t lastSuccessAgeMs = 0;
  bool lastSuccessAvailable = false;
  uint32_t attempts = 0;
  uint32_t successes = 0;
  int lastHttpStatus = 0;
  uint32_t nextFetchInMs = 0;
  char message[64] = "";
};

void rainAlertServiceBegin();
// Zastaví dotazy před aktualizací firmwaru; znovu se nerozběhnou.
void rainAlertServicePrepareForFirmwareUpdate();

// Zapnutí, adresa serveru, poloha a nastavení dotazu. Volá se klidně v každé
// smyčce - dotaz navíc vyvolá jen skutečná změna.
void rainAlertServiceSetActive(bool enabled, const char *feedUrl,
                               float latitude, float longitude,
                               uint8_t radiusKm, uint8_t wideKm,
                               uint8_t refreshMinutes);

// Zkopíruje poslední předpověď i s přehledem. Data starší než tři dotazy se
// nevydávají: o současném stavu už nic neříkají.
void rainAlertServiceStatus(RainAlertStatus &status);
