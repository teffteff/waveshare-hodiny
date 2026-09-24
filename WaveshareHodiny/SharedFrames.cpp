#include "SharedFrames.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace {

portMUX_TYPE framesMux = portMUX_INITIALIZER_UNLOCKED;
uint16_t *frames[SHARED_FRAME_COUNT] = {};
SharedFrameUser owner = SharedFrameUser::None;
// Kdo právě kreslí. Pár se smí předat i během kreslení, nový majitel ale
// začne až po jeho konci, aby si nepřepisovali buffer pod rukama.
SharedFrameUser renderingUser = SharedFrameUser::None;
// Roste s každým předáním; spolu s majitelem tvoří zápůjčku.
uint32_t leaseCounter = 1;

}  // namespace

bool sharedFramesReserve() {
  portENTER_CRITICAL(&framesMux);
  const bool ready = frames[SHARED_FRAME_COUNT - 1] != nullptr;
  portEXIT_CRITICAL(&framesMux);
  if (ready) return true;
  // Alokuje jen úloha, která kreslí, nebo start; souběh dvou alokací by
  // nanejvýš jeden buffer uvolnil zbytečně, proto se vkládá pod zámkem.
  for (size_t index = 0; index < SHARED_FRAME_COUNT; ++index) {
    portENTER_CRITICAL(&framesMux);
    const bool have = frames[index] != nullptr;
    portEXIT_CRITICAL(&framesMux);
    if (have) continue;
    auto *buffer = static_cast<uint16_t *>(heap_caps_calloc(
        SHARED_FRAME_WIDTH * SHARED_FRAME_HEIGHT, sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) return false;
    portENTER_CRITICAL(&framesMux);
    const bool stored = frames[index] == nullptr;
    if (stored) frames[index] = buffer;
    portEXIT_CRITICAL(&framesMux);
    if (!stored) heap_caps_free(buffer);
  }
  return true;
}

uint16_t *sharedFrame(size_t index) {
  if (index >= SHARED_FRAME_COUNT) return nullptr;
  portENTER_CRITICAL(&framesMux);
  uint16_t *frame = frames[index];
  portEXIT_CRITICAL(&framesMux);
  return frame;
}

void sharedFramesClaim(SharedFrameUser user) {
  portENTER_CRITICAL(&framesMux);
  if (owner != user) {
    owner = user;
    ++leaseCounter;
  }
  portEXIT_CRITICAL(&framesMux);
}

uint32_t sharedFramesBeginRender(SharedFrameUser user) {
  if (user == SharedFrameUser::None) return 0;
  uint32_t lease = 0;
  portENTER_CRITICAL(&framesMux);
  if (owner == SharedFrameUser::None) {
    owner = user;
    ++leaseCounter;
  }
  if (owner == user && frames[SHARED_FRAME_COUNT - 1] != nullptr &&
      (renderingUser == SharedFrameUser::None || renderingUser == user)) {
    renderingUser = user;
    lease = leaseCounter;
  }
  portEXIT_CRITICAL(&framesMux);
  return lease;
}

void sharedFramesEndRender(SharedFrameUser user) {
  portENTER_CRITICAL(&framesMux);
  if (renderingUser == user) renderingUser = SharedFrameUser::None;
  portEXIT_CRITICAL(&framesMux);
}

bool sharedFramesLeaseValid(SharedFrameUser user, uint32_t lease) {
  portENTER_CRITICAL(&framesMux);
  const bool valid = lease != 0 && owner == user && leaseCounter == lease;
  portEXIT_CRITICAL(&framesMux);
  return valid;
}
