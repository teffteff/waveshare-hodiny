// Sledování velkých alokací pro diagnostiku paměti (jen build MEMTRACE=1).
// Linker přesměruje heap_caps_* a malloc/free sem (-Wl,--wrap=...); každý
// živý blok od MEMTRACE_MIN_BYTES výš se zapíše i se třemi návratovými
// adresami. Ty se pak přeloží na funkce přes addr2line nad .elf.
#include "MemTrace.h"

#if HODINY_MEMTRACE

#include <esp_debug_helpers.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <freertos/FreeRTOS.h>

namespace {
MemTraceEntry entries[MEMTRACE_CAPACITY];
portMUX_TYPE traceMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t droppedCount = 0;

void capture(uint32_t (&pcs)[MEMTRACE_DEPTH]) {
  for (uint32_t &pc : pcs) pc = 0;
  esp_backtrace_frame_t frame = {};
  esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
  // První dva rámce jsou capture() a obalovací funkce.
  for (int skip = 0; skip < 2; ++skip)
    if (!esp_backtrace_get_next_frame(&frame)) return;
  for (int index = 0; index < MEMTRACE_DEPTH; ++index) {
    // Okénkové ABI Xtensy nese v horních dvou bitech velikost okna.
    pcs[index] = (frame.pc & 0x3fffffff) | 0x40000000;
    if (frame.next_pc == 0 || !esp_backtrace_get_next_frame(&frame)) return;
  }
}

void track(void *pointer, size_t size) {
  if (pointer == nullptr || size < MEMTRACE_MIN_BYTES) return;
  uint32_t pcs[MEMTRACE_DEPTH];
  capture(pcs);
  portENTER_CRITICAL_SAFE(&traceMux);
  bool stored = false;
  for (MemTraceEntry &entry : entries) {
    if (entry.pointer != nullptr) continue;
    entry.pointer = pointer;
    entry.size = size;
    entry.psram = esp_ptr_external_ram(pointer);
    for (int index = 0; index < MEMTRACE_DEPTH; ++index) entry.pcs[index] = pcs[index];
    stored = true;
    break;
  }
  if (!stored) ++droppedCount;
  portEXIT_CRITICAL_SAFE(&traceMux);
}

void untrack(void *pointer) {
  if (pointer == nullptr) return;
  portENTER_CRITICAL_SAFE(&traceMux);
  for (MemTraceEntry &entry : entries)
    if (entry.pointer == pointer) entry.pointer = nullptr;
  portEXIT_CRITICAL_SAFE(&traceMux);
}
}  // namespace

size_t memTraceSnapshot(MemTraceEntry *output, size_t capacity, uint32_t &dropped) {
  size_t count = 0;
  portENTER_CRITICAL_SAFE(&traceMux);
  for (const MemTraceEntry &entry : entries)
    if (entry.pointer != nullptr && count < capacity) output[count++] = entry;
  dropped = droppedCount;
  portEXIT_CRITICAL_SAFE(&traceMux);
  return count;
}

extern "C" {
void *__real_heap_caps_malloc(size_t size, uint32_t caps);
void *__real_heap_caps_calloc(size_t count, size_t size, uint32_t caps);
void *__real_heap_caps_realloc(void *pointer, size_t size, uint32_t caps);
void __real_heap_caps_free(void *pointer);
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);

void *__wrap_heap_caps_malloc(size_t size, uint32_t caps) {
  void *pointer = __real_heap_caps_malloc(size, caps);
  track(pointer, size);
  return pointer;
}
void *__wrap_heap_caps_calloc(size_t count, size_t size, uint32_t caps) {
  void *pointer = __real_heap_caps_calloc(count, size, caps);
  track(pointer, count * size);
  return pointer;
}
void *__wrap_heap_caps_realloc(void *pointer, size_t size, uint32_t caps) {
  void *resized = __real_heap_caps_realloc(pointer, size, caps);
  if (resized != nullptr || size == 0) untrack(pointer);
  track(resized, size);
  return resized;
}
void __wrap_heap_caps_free(void *pointer) {
  untrack(pointer);
  __real_heap_caps_free(pointer);
}
void *__wrap_malloc(size_t size) {
  void *pointer = __real_malloc(size);
  track(pointer, size);
  return pointer;
}
void *__wrap_calloc(size_t count, size_t size) {
  void *pointer = __real_calloc(count, size);
  track(pointer, count * size);
  return pointer;
}
void *__wrap_realloc(void *pointer, size_t size) {
  void *resized = __real_realloc(pointer, size);
  if (resized != nullptr || size == 0) untrack(pointer);
  track(resized, size);
  return resized;
}
void __wrap_free(void *pointer) {
  untrack(pointer);
  __real_free(pointer);
}
}

#endif  // HODINY_MEMTRACE
