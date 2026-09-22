#pragma once

#include <Arduino.h>

#include <cstdint>

// Záznam o posledním pádu hodin. Po panice zapíše ESP-IDF výpis paměti do
// oddílu coredump, jenže ten přečte jen esptool přes USB - a hodiny na zdi
// u USB nejsou. Při startu se proto z výpisu vytáhne souhrn (úloha, adresa,
// zpětná stopa), uloží se do NVS a výpis se smaže, aby se stejný pád
// nepočítal dvakrát. Diagnostika ho pak ukáže i po dalších restartech včetně
// OTA. Adresy převede na řádky zdrojáku tools/decode-crash.sh.

constexpr size_t CRASH_BACKTRACE_MAX = 16;

struct CrashRecord {
  uint8_t version;
  // esp_reset_reason() startu, který záznam našel. Výpis z dřívějška (např.
  // pád těsně před OTA) se najde až po softwarovém restartu, takže tu pak
  // bude 3, ne 4.
  uint8_t resetReason;
  bool hasDump;
  bool backtraceCorrupted;
  uint8_t backtraceDepth;
  char task[16];
  // Prvních 16 hex znaků SHA-256 ELF souboru spadlého firmwaru; podle něj
  // tools/decode-crash.sh ověří, že dekóduje proti správnému sestavení.
  char elfSha[17];
  char firmware[24];
  char reason[96];
  uint32_t pc;
  uint32_t exceptionCause;
  uint32_t exceptionAddress;
  uint32_t backtrace[CRASH_BACKTRACE_MAX];
};

// Resety, které znamenají, že firmware nedoběhl sám: panika, watchdogy
// a podpětí. Běžný restart (OTA, uložení nastavení, tlačítko) sem nepatří.
bool crashLogAbnormalReset(uint8_t resetReason);

// Zapíše pád do NVS, pokud ho tento start našel: buď je k dispozici výpis
// (dump), nebo byl reset abnormální. Vrací true, když se záznam uložil nebo
// nebylo co ukládat; false jen při chybě zápisu, aby volající výpis nemazal.
bool crashLogRecord(const CrashRecord *dump, uint8_t resetReason,
                    const char *firmwareVersion);

// {"count":N,"last":{...}} nebo {"count":0,"last":null}.
String crashLogJson();

// Na hodinách: přečte oddíl coredump, zapíše ho a smaže. Volat před
// spuštěním displeje, protože maže flash.
void crashLogBegin();
