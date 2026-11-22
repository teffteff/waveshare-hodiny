// Stav zdrojů dat a společný řádek "Zdroj nedostupný" (SourceHealth.h).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "SourceHealth.h"

namespace {

std::string text(const SourceHealth &health, uint32_t now, bool stale,
                 bool english = false) {
  char buffer[96];
  sourceHealthFormat(health, now, stale, english, buffer, sizeof(buffer));
  return buffer;
}

void testFreshSourceIsNotDown() {
  SourceHealth health;
  assert(!health.down());
  assert(health.ageMs(1000) == UINT32_MAX);
  assert(health.nextInMs(1000) == 0);
  assert(text(health, 1000, false).empty());
  health.record(1000, true, 600000);
  assert(!health.down());
  assert(health.ageMs(1500) == 500);
  assert(health.nextInMs(1000) == 600000);
}

void testFirstFailureWithoutDataIsDown() {
  SourceHealth health;
  health.record(0, false, 120000);
  assert(health.down());
  assert(text(health, 0, false) ==
         "Zdroj nedostupný · další pokus za 2 min");
  // Minuty se zaokrouhlují nahoru, pod minutu sekundy.
  assert(text(health, 1000, false) ==
         "Zdroj nedostupný · další pokus za 2 min");
  assert(text(health, 61000, false) ==
         "Zdroj nedostupný · další pokus za 59 s");
  assert(text(health, 119001, false) ==
         "Zdroj nedostupný · další pokus za 1 s");
  assert(text(health, 120000, false) == "Zdroj nedostupný · zkouším znovu");
  assert(text(health, 0, false, true) ==
         "Source unavailable · next try in 2 min");
}

void testOneBlipAfterSuccessIsNotReported() {
  SourceHealth health;
  health.record(0, true, 5000);
  health.record(5000, false, 5000);
  assert(!health.down());
  health.record(10000, false, 5000);
  assert(health.down());
  assert(health.failures == 2);
  // Zdroj odpovídal, obrazovka drží jeho poslední data.
  assert(text(health, 10000, true) ==
         "Stará data · zdroj nedostupný · další pokus za 5 s");
  assert(text(health, 10000, true, true) ==
         "Old data · source unavailable · next try in 5 s");
  assert(health.ageMs(10000) == 10000);
  // Úspěch vše smaže.
  health.record(15000, true, 5000);
  assert(!health.down());
  assert(health.failures == 0);
  assert(text(health, 15000, true).empty());
}

void testResetForgets() {
  SourceHealth health;
  health.record(0, false, 1000);
  assert(health.down());
  health.reset();
  assert(!health.down());
  assert(!health.tried);
}

void testMillisWrap() {
  SourceHealth health;
  const uint32_t nearWrap = 0xFFFFFFFFu - 500u;
  health.record(nearWrap, false, 30000);
  assert(health.nextInMs(nearWrap + 1000u) == 29000);
  assert(health.nextInMs(nearWrap + 30000u) == 0);
}

void testSmallBuffer() {
  SourceHealth health;
  health.record(0, false, 120000);
  char tiny[8];
  const size_t length =
      sourceHealthFormat(health, 0, false, false, tiny, sizeof(tiny));
  assert(length == sizeof(tiny) - 1);
  assert(strlen(tiny) == sizeof(tiny) - 1);
}

void testNames() {
  for (size_t index = 0; index < CLOCK_SOURCE_COUNT; ++index)
    assert(sourceHealthName(static_cast<ClockSource>(index))[0] != '\0');
}

}  // namespace

int main() {
  testFreshSourceIsNotDown();
  testFirstFailureWithoutDataIsDown();
  testOneBlipAfterSuccessIsNotReported();
  testResetForgets();
  testMillisWrap();
  testSmallBuffer();
  testNames();
  std::puts("source_health OK");
  return 0;
}
