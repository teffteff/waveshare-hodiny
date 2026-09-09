#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ChmiFrameNames.h"

namespace {

// 9. 9. 2026 v 17:42:13 UTC; slot, do kterého to padne, začíná v 17:40.
constexpr time_t SAMPLE_MOMENT = 1788975733;
constexpr time_t SAMPLE_SLOT = 1788975600;

void testSlotRoundsDown() {
  assert(chmiFrameSlotAt(SAMPLE_MOMENT) == SAMPLE_SLOT);
  // Hranice patří novému slotu, ne tomu předchozímu.
  assert(chmiFrameSlotAt(SAMPLE_SLOT) == SAMPLE_SLOT);
  assert(chmiFrameSlotAt(SAMPLE_SLOT - 1) == SAMPLE_SLOT - CHMI_FRAME_SLOT_SECONDS);
  assert(chmiFrameSlotAt(SAMPLE_SLOT + CHMI_FRAME_SLOT_SECONDS - 1) == SAMPLE_SLOT);
}

void testNameMatchesTheServer() {
  char name[CHMI_FRAME_NAME_CAPACITY] = "";
  chmiFrameName(SAMPLE_SLOT, name, sizeof(name));
  // Doslovné jméno z výpisu opendata.chmi.cz. Kdyby se rozešlo o jediný znak,
  // radar by nestáhl vůbec nic.
  assert(std::string(name) == "pacz2gmaps3.z_max3d.20260909.1740.0.png");
  assert(strlen(name) < CHMI_FRAME_NAME_CAPACITY);
}

void testNameIsUtcNotLocalTime() {
  // Zdejší čas se do jména nesmí promítnout. Na stroji v Praze je v tuhle
  // chvíli 19:40 letního času, takže by chybné jméno neslo 1940 a ukazovalo
  // na snímek, který vyjde až za dvě hodiny.
  setenv("TZ", "Europe/Prague", 1);
  tzset();
  char name[CHMI_FRAME_NAME_CAPACITY] = "";
  chmiFrameName(SAMPLE_SLOT, name, sizeof(name));
  assert(std::string(name) == "pacz2gmaps3.z_max3d.20260909.1740.0.png");
  setenv("TZ", "UTC", 1);
  tzset();
}

void testWindowRunsOldestFirst() {
  constexpr size_t FRAMES = 6;
  char names[FRAMES][CHMI_FRAME_NAME_CAPACITY] = {};
  assert(chmiFrameNamesEndingAt(SAMPLE_SLOT, names, FRAMES));
  // Animace čte pole odpředu, takže nejstarší snímek musí být první a poslední
  // ten, kterým se okno zavírá.
  assert(std::string(names[FRAMES - 1]) ==
         "pacz2gmaps3.z_max3d.20260909.1740.0.png");
  assert(std::string(names[0]) == "pacz2gmaps3.z_max3d.20260909.1715.0.png");
  for (size_t index = 1; index < FRAMES; ++index) {
    assert(strcmp(names[index - 1], names[index]) < 0);
  }
}

void testWindowCrossesMidnight() {
  // Přes půlnoc se musí přepnout i datum, ne jen čas. Slot 00:00 dne
  // 10. 9. 2026 s pěti snímky sahá do předchozího dne.
  const time_t midnight = SAMPLE_SLOT + 6 * 3600 + 20 * 60;
  constexpr size_t FRAMES = 5;
  char names[FRAMES][CHMI_FRAME_NAME_CAPACITY] = {};
  assert(chmiFrameNamesEndingAt(midnight, names, FRAMES));
  assert(std::string(names[FRAMES - 1]) ==
         "pacz2gmaps3.z_max3d.20260910.0000.0.png");
  assert(std::string(names[0]) == "pacz2gmaps3.z_max3d.20260909.2340.0.png");
}

void testEmptyWindowIsRejected() {
  char names[1][CHMI_FRAME_NAME_CAPACITY] = {};
  assert(!chmiFrameNamesEndingAt(SAMPLE_SLOT, names, 0));
}

}  // namespace

int main() {
  testSlotRoundsDown();
  testNameMatchesTheServer();
  testNameIsUtcNotLocalTime();
  testWindowRunsOldestFirst();
  testWindowCrossesMidnight();
  testEmptyWindowIsRejected();
  printf("chmi_frame_names OK\n");
  return 0;
}
