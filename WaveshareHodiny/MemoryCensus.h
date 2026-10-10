#pragma once

#include <Arduino.h>

// Sčítání paměti pro diagnostiku: volná PSRAM a vnitřní RAM po jednotlivých
// krocích startu a kolik si drží LVGL. Na 7" je PSRAM po kódu v ní (XIP)
// jen 4,8 MB a se všemi obrazovkami došla; bez rozpisu by se škrtalo naslepo.

struct MemoryCheckpoint {
  const char *name;
  uint32_t atMs;
  uint32_t psramFree;
  uint32_t internalFree;
};

constexpr size_t MEMORY_CHECKPOINT_CAPACITY = 32;

// Zapíše stav paměti pod jménem (řetězec musí žít napořád - literál).
// Opakované jméno se nepřidá znovu; drží se první výskyt.
void memoryCensusCheckpoint(const char *name);
size_t memoryCensusCheckpoints(const MemoryCheckpoint *&checkpoints);

// Kolik bajtů má LVGL právě přidělených v PSRAM a ve vnitřní RAM.
void memoryCensusLvgl(uint32_t &psramBytes, uint32_t &internalBytes);
// Volá alokátor LVGL (ClockLvglMemory.cpp).
void memoryCensusLvglAllocated(void *pointer, int sign);
