#pragma once

#include <Arduino.h>

#include "NetPollSchedule.h"

// Společné úlohy pro malé síťové dotazy.
//
// Dřív měla každá služba, která jen čeká a pak stáhne malý JSON, vlastní
// úlohu se zásobníkem 12-20 kB v PSRAM; na 7", kde PSRAM chybí nejvíc, to
// dělalo přes sto kilobajtů. Teď jsou to kroky (NetPollStep) ve dvou
// pracovnících s plánovačem termínů (NetPollSchedule.h):
//
// - Alerts ("net-poll"): blesky, déšť, výstrahy ČHMÚ, upozornění. Krátké
//   dotazy, na kterých závisí přepnutí obrazovky na déšť nebo bouřku.
// - Text ("net-text"): agenda (i přenos zálohy), rozvrh, předpověď s historií
//   a zprávy, včetně zkoušek z webu. Stažení zpráv nebo předpovědi trvá
//   i desítky sekund a nesmí zdržet výstrahy, proto má text vlastního
//   pracovníka.
//
// Uvnitř pracovníka rozhoduje pořadí registrace (dřív = vyšší priorita).
// Stahování všech služeb stejně řadí NetworkOperationGuard, takže dva
// pracovníci místo jednoho nic neztrácejí ani souběhem spojení.
//
// Krok běží v pracovníkovi, nikdy ne v LVGL; vrací, za kolik milisekund ho
// zavolat znovu (0 = hned, NET_POLL_IDLE = až po netPollWake). woken říká,
// že od minulého kroku přišlo netPollWake - to dřív dělala notifikace úlohy.
// Probuzení během běhu kroku se neztratí: krok se pak zavolá hned znovu.

enum class NetPollWorker : uint8_t { Alerts = 0, Text = 1 };

using NetPollStep = uint32_t (*)(bool woken);

// Zaregistruje krok a případně spustí pracovníka. Vrací číslo úlohy pro
// netPollWake, nebo -1 (plno, nebo pracovník nevznikl).
int netPollRegister(NetPollWorker worker, const char *name, NetPollStep step);
// Probudí úlohu; bezpečné z kterékoli úlohy (ne z přerušení). Záporné číslo
// se ignoruje.
void netPollWake(int job);
// Kratší z čekání a taktu: služby, které dřív v úloze hlídaly Wi-Fi a stav po
// taktech, se tak budí stejně často jako předtím.
inline uint32_t netPollWithin(uint32_t waitMs, uint32_t tickMs) {
  return waitMs < tickMs ? waitMs : tickMs;
}

// Pro diagnostiku zásobníku (taskStacks); nullptr, dokud pracovník neběží.
TaskHandle_t netPollWorkerTask(NetPollWorker worker);
// "netPoll":[...] bez úvodní čárky: pracovníci, jejich úlohy, poslední běh,
// zpoždění a nejmenší volný zásobník.
void netPollAppendDiagnosticsJson(String &result);
