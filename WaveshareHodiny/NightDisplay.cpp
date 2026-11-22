#include "NightDisplay.h"

namespace {

bool before(uint32_t a, uint32_t b) {
  return static_cast<int32_t>(a - b) < 0;
}

}  // namespace

bool NightDisplay::touch(uint32_t now, uint32_t wakeMs) {
  const bool wasDark = dark_;
  wakeFor(now, wakeMs);
  dark_ = false;
  return wasDark;
}

void NightDisplay::wakeFor(uint32_t now, uint32_t ms) {
  // Nula by rozsvícení ukončila hned; nejméně jedna milisekunda.
  if (ms == 0) ms = 1;
  // Delší dobu přes půl rozsahu millis() porovnání rozdílem neunese.
  if (ms > 0x7FFFFFFFu) ms = 0x7FFFFFFFu;
  const uint32_t until = now + ms;
  if (!awake_ || before(awakeUntil_, until)) awakeUntil_ = until;
  awake_ = true;
}

bool NightDisplay::update(uint32_t now, bool enabled, bool night,
                          bool keepAwake) {
  if (!enabled || !night) {
    // Den nebo vypnutá volba: displej svítí a rozsvícení z noci se zapomene,
    // aby příští večer nezačal "probuzený".
    awake_ = false;
    dark_ = false;
    return false;
  }
  if (awake_ && !before(now, awakeUntil_)) awake_ = false;
  dark_ = !keepAwake && !awake_;
  return dark_;
}

uint32_t NightDisplay::awakeRemainingMs(uint32_t now) const {
  if (!awake_ || !before(now, awakeUntil_)) return 0;
  return awakeUntil_ - now;
}
