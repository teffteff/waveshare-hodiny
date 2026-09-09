// Náhrada alokátoru ESP-IDF pro testy na počítači. PSRAM na hostiteli není,
// takže se všechny příznaky ignorují a paměť dá obyčejný malloc.
#pragma once

#include <cstdlib>

#define MALLOC_CAP_8BIT 0x0004
#define MALLOC_CAP_SPIRAM 0x0400

inline void *heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
inline void heap_caps_free(void *pointer) { free(pointer); }
