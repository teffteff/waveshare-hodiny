#pragma once

#include <Arduino.h>

// Paměť pro mbedTLS v PSRAM.
//
// Jádro Arduina je přeložené s CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC, takže každý
// TLS handshake si bere dva souvislé 16kB buffery a kontexty z vnitřní SRAM.
// Té má deska volných kolem 100 kB, ale největší souvislý blok bývá jen okolo
// 56 kB, a jakmile ho něco rozdrobí, padají HTTPS dotazy všech služeb najednou
// s chybou, která vypadá jako problém certifikátu.
//
// mbedTLS si nechá alokátor vyměnit za běhu. Výměna směruje jeho paměť do
// PSRAM, kde místa zbývají megabajty; vnitřní SRAM zůstane Wi-Fi, LVGL a
// zásobníkům. Uvolňuje se obyčejným free(), které v ESP-IDF pozná obě haldy,
// takže bloky přidělené ještě před výměnou se vrátí správně.
//
// Hardwarové AES a SHA si pro DMA přenosy berou vlastní vnitřní buffery, když
// jim přijdou data z PSRAM; nic dalšího se kvůli tomu hlídat nemusí.
void tlsMemoryBegin();

// Kolikrát PSRAM nestačila a blok musel do vnitřní SRAM. Nula je normální.
uint32_t tlsMemoryInternalFallbackCount();
