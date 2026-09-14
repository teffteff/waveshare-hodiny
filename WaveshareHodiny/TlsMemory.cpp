#include "TlsMemory.h"

#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

#include <atomic>

namespace {
std::atomic<uint32_t> internalFallbacks{0};

void *tlsCalloc(size_t count, size_t size) {
  void *block =
      heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (block != nullptr) return block;
  // Bez PSRAM je lepší zkusit vnitřní SRAM než handshake rovnou shodit.
  block = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (block != nullptr) internalFallbacks.fetch_add(1);
  return block;
}

void tlsFree(void *block) { free(block); }
}  // namespace

void tlsMemoryBegin() {
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) return;
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
}

uint32_t tlsMemoryInternalFallbackCount() { return internalFallbacks.load(); }
