#pragma once

#include <Arduino.h>

#include "ClockConfig.h"

// Předání nastavení upozornění na telefon serveru (infra/alerts).
//
// Hodiny nic nehlídají: server hlídá déšť i letadla sám a posílá push, i když
// jsou hodiny vypnuté. Tahle služba mu jen pošle, co hlídat. Pošle to, kdykoli
// se chtěné nastavení liší od toho, které server naposledy potvrdil - tedy po
// uložení, po startu hodin a znovu po chybě, dokud server nepřijme.
// Vypnutá upozornění se posílají taky, aby server přestal hlídat.
//
// Vlastní úloha ze stejného důvodu jako u srážek: adresu zadává majitel, takže
// se ověřuje proti svazku kořenů Mozilly a na to je potřeba velký zásobník.

void pushAlertsServiceBegin();
void pushAlertsServicePrepareForFirmwareUpdate();

// Volá se klidně po každém uložení; dotaz vyvolá jen skutečná změna.
void pushAlertsServiceSetConfig(const ClockPushAlertsConfig &alerts,
                                float latitude, float longitude,
                                const char *deviceName);

// Poslední stav pro stránku nastavení, třeba "Server nastavení přijal".
void pushAlertsServiceMessage(char *message, size_t capacity);
