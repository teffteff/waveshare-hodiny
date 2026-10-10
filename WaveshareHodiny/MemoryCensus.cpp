#include "MemoryCensus.h"

#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

namespace {
MemoryCheckpoint checkpoints[MEMORY_CHECKPOINT_CAPACITY];
size_t checkpointCount = 0;
portMUX_TYPE censusMux = portMUX_INITIALIZER_UNLOCKED;
// Alokátor LVGL běží jen v úloze loop, ale diagnostika čte z webové úlohy.
volatile int32_t lvglPsram = 0;
volatile int32_t lvglInternal = 0;
}  // namespace

void memoryCensusCheckpoint(const char *name) {
  const uint32_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const uint32_t internal =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  portENTER_CRITICAL(&censusMux);
  bool known = false;
  for (size_t index = 0; index < checkpointCount; ++index)
    if (checkpoints[index].name == name) known = true;
  if (!known && checkpointCount < MEMORY_CHECKPOINT_CAPACITY)
    checkpoints[checkpointCount++] = {name, millis(), psram, internal};
  portEXIT_CRITICAL(&censusMux);
}

size_t memoryCensusCheckpoints(const MemoryCheckpoint *&result) {
  result = checkpoints;
  return checkpointCount;
}

void memoryCensusLvgl(uint32_t &psramBytes, uint32_t &internalBytes) {
  psramBytes = static_cast<uint32_t>(lvglPsram > 0 ? lvglPsram : 0);
  internalBytes = static_cast<uint32_t>(lvglInternal > 0 ? lvglInternal : 0);
}

void memoryCensusLvglAllocated(void *pointer, int sign) {
  if (pointer == nullptr) return;
  const int32_t size =
      static_cast<int32_t>(heap_caps_get_allocated_size(pointer)) * sign;
  if (esp_ptr_external_ram(pointer))
    lvglPsram += size;
  else
    lvglInternal += size;
}
