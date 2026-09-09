#include "ChmiFrameNames.h"

#include <stdio.h>

namespace {
constexpr char FILE_PREFIX[] = "pacz2gmaps3.z_max3d.";
}  // namespace

time_t chmiFrameSlotAt(time_t moment) {
  // Zbytek po dělení je u záporných časů implementačně závislý, ale ty sem
  // nechodí: hodiny se ptají až po synchronizaci s NTP.
  if (moment < 0) return moment;
  return moment - moment % CHMI_FRAME_SLOT_SECONDS;
}

void chmiFrameName(time_t slot, char *output, size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  output[0] = '\0';
  struct tm utc = {};
  if (gmtime_r(&slot, &utc) == nullptr) return;
  char stamp[16] = "";
  if (strftime(stamp, sizeof(stamp), "%Y%m%d.%H%M", &utc) == 0) return;
  snprintf(output, capacity, "%s%s.0.png", FILE_PREFIX, stamp);
}

bool chmiFrameNamesEndingAt(time_t newestSlot,
                            char names[][CHMI_FRAME_NAME_CAPACITY],
                            size_t count) {
  if (names == nullptr || count == 0) return false;
  for (size_t index = 0; index < count; ++index) {
    const time_t slot =
        newestSlot -
        static_cast<time_t>(count - 1 - index) * CHMI_FRAME_SLOT_SECONDS;
    chmiFrameName(slot, names[index], CHMI_FRAME_NAME_CAPACITY);
    if (names[index][0] == '\0') return false;
  }
  return true;
}
