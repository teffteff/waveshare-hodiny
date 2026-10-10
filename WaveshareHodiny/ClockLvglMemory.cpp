#include "ClockLvglMemory.h"

#include <esp_heap_caps.h>

#include "MemoryCensus.h"

namespace {
constexpr size_t PSRAM_ALLOCATION_THRESHOLD = 4096;
// Přepíná se jen z úlohy loop, ve které běží celé LVGL, takže se o souběh
// starat nemusí.
bool preferPsram = false;

uint32_t capabilitiesForSize(size_t size) {
  return preferPsram || size >= PSRAM_ALLOCATION_THRESHOLD
             ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
             : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
}
}  // namespace

extern "C" void clockLvglPreferPsram(bool prefer) { preferPsram = prefer; }

extern "C" void *clockLvglAlloc(size_t size) {
  void *pointer = heap_caps_malloc(size, capabilitiesForSize(size));
  if (pointer == nullptr && size < PSRAM_ALLOCATION_THRESHOLD) {
    pointer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  memoryCensusLvglAllocated(pointer, 1);
  return pointer;
}

extern "C" void *clockLvglRealloc(void *pointer, size_t size) {
  memoryCensusLvglAllocated(pointer, -1);
  void *resized = heap_caps_realloc(pointer, size, capabilitiesForSize(size));
  if (resized == nullptr && size < PSRAM_ALLOCATION_THRESHOLD) {
    resized = heap_caps_realloc(pointer, size,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  // Nepovedený realloc nechá původní blok platný.
  memoryCensusLvglAllocated(resized != nullptr ? resized : pointer, 1);
  return resized;
}

extern "C" void clockLvglFree(void *pointer) {
  memoryCensusLvglAllocated(pointer, -1);
  heap_caps_free(pointer);
}
