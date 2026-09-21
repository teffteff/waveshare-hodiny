#pragma once

#include <Arduino.h>

#include "WeatherWarnings.h"

// Výstrahy ČHMÚ z vlastního serveru (infra/warnings).
//
// CAP soubor ČHMÚ má dva megabajty, takže ho stahuje server, převede polohu
// hodin na ORP a pošle jen výstrahy, které se jí týkají. Tahle služba dělá
// jeden malý dotaz za deset minut. Co ukázat na radaru a kdy přepnout
// obrazovku, rozhoduje smyčka v hodinách, stejně jako u deště.
//
// Vlastní úloha je tu ze stejného důvodu jako u deště a blesků: adresu zadává
// majitel, takže se ověřuje proti svazku kořenů Mozilly, a na to je potřeba
// výrazně větší zásobník, než má úloha home-assistant.

struct WeatherWarningStatus {
  // Roste s každou úspěšnou odpovědí, aby obrazovka poznala změnu.
  uint32_t generation = 0;
  bool enabled = false;
  bool ready = false;
  WeatherWarningFeed feed;
  uint32_t lastSuccessAgeMs = 0;
  bool lastSuccessAvailable = false;
  uint32_t attempts = 0;
  uint32_t successes = 0;
  int lastHttpStatus = 0;
  uint32_t nextFetchInMs = 0;
  char message[64] = "";
};

void weatherWarningServiceBegin();
// Zastaví dotazy před aktualizací firmwaru; znovu se nerozběhnou.
void weatherWarningServicePrepareForFirmwareUpdate();

// Zapnutí, adresa serveru, poloha a jazyk. Volá se klidně v každé smyčce -
// dotaz navíc vyvolá jen skutečná změna.
void weatherWarningServiceSetActive(bool enabled, const char *feedUrl,
                                    float latitude, float longitude,
                                    bool english, uint8_t refreshMinutes);

// Zkopíruje poslední výstrahy i s přehledem. Data starší než šest dotazů se
// nevydávají: výstraha mezitím mohla skončit nebo přibýt jiná.
void weatherWarningServiceStatus(WeatherWarningStatus &status);
