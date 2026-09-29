#pragma once

#include <Arduino.h>

#include "ClockConfig.h"
#include "PushAlerts.h"

// Upozornění na telefon ze serveru (majnr infra/alerts).
//
// Hodiny nic nehlídají ani nenastavují: server hlídá déšť, letadla a výstrahy
// ČHMÚ sám a posílá push, i když jsou hodiny vypnuté. Nastavení je jedno pro
// celou domácnost a mění se na webu serveru. Tahle služba si ho jen čte:
// obrazovka letadel podle něj označuje nízký přelet stejně jako server
// a stránka nastavení ho ukazuje. Čte se po startu, po změně adresy nebo
// polohy a pak každou půlhodinu.
//
// Vlastní úloha ze stejného důvodu jako u srážek: adresu zadává majitel, takže
// se ověřuje proti svazku kořenů Mozilly a na to je potřeba velký zásobník.

void pushAlertsServiceBegin();
void pushAlertsServicePrepareForFirmwareUpdate();

// Volá se klidně po každém uložení; dotaz vyvolá jen skutečná změna.
void pushAlertsServiceSetConfig(const ClockPushAlertsConfig &alerts,
                                float latitude, float longitude);

// Stránka nastavení se otevřela: přečíst znovu, ať ukazuje čerstvý stav.
// Nejvýš jednou za minutu.
void pushAlertsServiceRefresh();

// Podle čeho obrazovka letadel pozná nízký přelet: tytéž meze jako push
// a nadmořská výška polohy hodin, obojí od serveru. valid jen se zapnutými
// upozorněními na nízké přelety a známou výškou.
struct PushAlertsLowPass {
  bool valid = false;
  float groundElevationM = 0.0f;
  uint16_t radiusM = 0;
  uint16_t heightM = 0;
};
void pushAlertsServiceLowPass(PushAlertsLowPass &lowPass);

// Naposledy přečtené nastavení; known = false, dokud se nepovedlo.
void pushAlertsServiceSettings(PushAlertsServerSettings &settings);

// Poslední stav pro stránku nastavení, třeba "Nastavení je načtené".
void pushAlertsServiceMessage(char *message, size_t capacity);
