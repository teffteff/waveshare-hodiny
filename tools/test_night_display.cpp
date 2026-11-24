// Pravidla zhasnutého displeje v noci (NightDisplay.h).
#include <cassert>
#include <cstdio>

#include "NightDisplay.h"

namespace {

constexpr uint32_t WAKE_MS = 30000;

void testDayAndDisabledNeverDark() {
  NightDisplay display;
  assert(!display.update(1000, false, true, false));
  assert(!display.update(1000, true, false, false));
  // Dotyk ve dne nic nezahazuje.
  assert(!display.touch(2000, WAKE_MS));
  assert(!display.update(2000, true, false, false));
}

void testNightGoesDarkAndTouchOnlyWakes() {
  NightDisplay display;
  assert(display.update(1000, true, true, false));
  assert(display.dark());
  // První dotyk na tmavý displej jen rozsvítí.
  assert(display.touch(1500, WAKE_MS));
  assert(!display.dark());
  assert(!display.update(1600, true, true, false));
  // Další dotyk na rozsvícený displej projde a dobu prodlouží.
  assert(!display.touch(20000, WAKE_MS));
  assert(!display.update(1500 + WAKE_MS + 100, true, true, false));
  assert(display.awakeRemainingMs(20000) == WAKE_MS);
  // Po uplynutí doby od posledního dotyku zhasne.
  assert(display.update(20000 + WAKE_MS, true, true, false));
  assert(display.awakeRemainingMs(20000 + WAKE_MS) == 0);
  // A další dotyk je zase jen probouzecí.
  assert(display.touch(20000 + WAKE_MS + 5, WAKE_MS));
}

void testAlertWakesForHoldTime() {
  NightDisplay display;
  assert(display.update(0, true, true, false));
  // Upozornění drží radar pět minut.
  display.wakeFor(1000, 5UL * 60UL * 1000UL);
  assert(!display.update(1000, true, true, false));
  assert(!display.update(1000 + 299999, true, true, false));
  // Kratší rozsvícení (dotyk) delší držení nezkrátí.
  assert(!display.touch(2000, WAKE_MS));
  assert(!display.update(2000 + WAKE_MS + 1, true, true, false));
  assert(display.update(1000 + 300000, true, true, false));
}

void testKeepAwake() {
  NightDisplay display;
  assert(display.update(0, true, true, false));
  assert(!display.update(10, true, true, true));
  // keepAwake neprobouzí na dobu, jen drží: po něm se hned zhasne.
  assert(display.update(20, true, true, false));
}

void testDayComesWhileDark() {
  NightDisplay display;
  assert(display.update(0, true, true, false));
  // Ráno: displej se rozsvítí bez dotyku.
  assert(!display.update(1000, true, false, false));
  assert(!display.dark());
  // Rozsvícení z noci se nepřenese do příštího večera.
  display.touch(2000, WAKE_MS);
  assert(!display.update(3000, true, false, false));
  assert(display.update(4000, true, true, false));
  // Vypnutí volby v noci displej rozsvítí také.
  assert(!display.update(5000, false, true, false));
}

void testMillisWrap() {
  NightDisplay display;
  const uint32_t nearWrap = 0xFFFFFFFFu - 1000u;
  assert(display.update(nearWrap, true, true, false));
  assert(display.touch(nearWrap, WAKE_MS));
  assert(!display.update(nearWrap + 10000u, true, true, false));
  assert(display.update(nearWrap + WAKE_MS, true, true, false));
}

void testZeroWakeStillWakes() {
  NightDisplay display;
  assert(display.update(0, true, true, false));
  assert(display.touch(100, 0));
  assert(!display.dark());
  assert(display.update(101, true, true, false));
}

}  // namespace

int main() {
  testDayAndDisabledNeverDark();
  testNightGoesDarkAndTouchOnlyWakes();
  testAlertWakesForHoldTime();
  testKeepAwake();
  testDayComesWhileDark();
  testMillisWrap();
  testZeroWakeStillWakes();
  std::puts("night_display OK");
  return 0;
}
